
import re

ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])/\n')


def cleaner(file_path):
    with open(file_path, "r") as f:
        with open("c_" + file_path, "w") as w:
            buffer = f.readlines()
            for line in buffer:
                line = "".join(line.split('W')[1:])
                mac = ""
                line = line.split(":")
                time_stamp = line[1].strip().split(',')[0]
                rssi = line[-1].split(',')[1]
                mac = line[1].strip().split(',')[1] + ":" + \
                    ":".join(line[2:-1]) + ":" + line[-1].split(',')[0]
                w.write(time_stamp + "," + mac + "," + rssi+"\n")
            




FILE_PATHS = ["rec.txt", "send.txt", "eve.txt"]

for file_name in FILE_PATHS:
    cleaner(file_name)

print("file cleaned")


buffer_rec = ""
buffer_send = ""
buffer_eve = ""

csv_buffer = "time,rssi_rec,rssi_send,rssi_eve_rec,rssi_eve_send\n"

with open("c_rec.txt", "r") as f:
    buffer_rec = f.readlines()

with open("c_send.txt", "r") as f:
    buffer_send = f.readlines()

with open("c_eve.txt", "r") as f:
    buffer_eve = f.readlines()

with open("out.csv", "w") as w:
    for index in range(0,len(buffer_rec)):
        elements = buffer_rec[index].split(",")
        csv_buffer += (elements[0]+","+elements[2][:-1])
        elements = buffer_send[index].split(",")
        csv_buffer += (","+elements[2][:-1])
        elements = buffer_eve[2*index].split(",")
        csv_buffer += (","+elements[2][:-1])
        elements = buffer_eve[2*index +1].split(",")
        csv_buffer += (","+elements[2][:-1])

        w.write(csv_buffer+"\n")
        csv_buffer  = ""


