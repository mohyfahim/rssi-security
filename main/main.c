#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#define SENDER // or EVE
// #define CONFIG_ESPNOW_ENABLE_LONG_RANGE

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA
#define ESPNOW_CHANNEL 1

static bool ready_to_send = true;

static uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static const char *TAG = "csi_security";

static long unsigned int timer_time = 0;

int temp = 0;
int *buffer = &temp;

static char *ESPNOW_PMK = "pmk1234567890123";

typedef struct
{
    unsigned frame_ctrl : 16;
    unsigned duration_id : 16;
    uint8_t addr1[6]; /* receiver address */
    uint8_t addr2[6]; /* sender address */
    uint8_t addr3[6]; /* filtering address */
    unsigned sequence_ctrl : 16;
    uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct
{
    wifi_ieee80211_mac_hdr_t hdr;
    uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;

void promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{

    // All espnow traffic uses action frames which are a subtype of the mgmnt frames so filter out everything else.
    if (type != WIFI_PKT_MGMT)
        return;

    static const uint8_t ACTION_SUBTYPE = 0xd0;
    static const uint8_t ESPRESSIF_OUI[] = {0x24, 0x0a, 0xc4, 0x24, 0x62, 0xab};

    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
    const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
    const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;
    char mac[20] = {0};

    // Only continue processing if this is an action frame containing the Espressif OUI.
    if ((ACTION_SUBTYPE == (hdr->frame_ctrl & 0xFF)) &&
        ((memcmp(hdr->addr2, ESPRESSIF_OUI, 3) == 0) || (memcmp(hdr->addr2, ESPRESSIF_OUI + 3, 3) == 0)))
    {

        sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", hdr->addr2[0], hdr->addr2[1], hdr->addr2[2], hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
        int rssi = ppkt->rx_ctrl.rssi;
        int data = -1;
        memcpy(&data, ipkt->payload + 7, sizeof(int));
        ESP_LOGW(TAG, "%lu,%s,%d,%d", (long unsigned int)esp_timer_get_time(), mac, rssi, data);
    }
}

void _vendor_ie_cb(void *ctx, wifi_vendor_ie_type_t type, const uint8_t sa[6], const vendor_ie_data_t *vnd_ie, int rssi)
{
    ESP_LOGW(TAG, "in ie: %d", rssi);
}




static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{

    long unsigned int rtt = (long unsigned int)(esp_timer_get_time() - timer_time);

    if (mac_addr == NULL || data == NULL || len <= 0)
    {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }
#if !defined(SENDER) && !defined(EVE)

    if (esp_now_send(BROADCAST_MAC, data, len) != ESP_OK)
    {
        ESP_LOGE(TAG, "Send error");
    }

#else
    if (*buffer == *(int *)data)
    {
        *buffer = *buffer + 1;
        ready_to_send = true;
    }
    ESP_LOGI(TAG, "Receive data from: " MACSTR " in %lu, and len : %d , message: %d", MAC2STR(mac_addr), rtt, len, *(int *)data);

#endif
}

void espnow_task(void *params)
{

    ESP_LOGI(TAG, "loop task start");
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (ready_to_send)
        {
            esp_err_t err = esp_now_send(BROADCAST_MAC, (uint8_t *)buffer, sizeof(int));

            if (err != ESP_OK)
            {
                // ESP_LOGE(TAG, "Send error %s", esp_err_to_name(err));
            }
            else
            {
                timer_time = esp_timer_get_time();
                ready_to_send = false;
                // ESP_LOGI(TAG, "sent successfully %d and %d", *buffer, temp);
            }
        }
    }
    vTaskDelete(NULL);
}
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;

    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(&promiscuous_rx_cb));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));


#ifdef CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
#endif
}

static esp_err_t espnow_init(void)
{

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    // ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    /* Set primary master key. */
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)ESPNOW_PMK));

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        ESP_LOGE(TAG, "Malloc peer information fail");
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, BROADCAST_MAC, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    espnow_init();

#ifdef SENDER
    xTaskCreate(espnow_task, "espnow_task", 2048, NULL, 1, NULL);
#endif
}
