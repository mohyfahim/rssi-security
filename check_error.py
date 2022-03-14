with open("eve.txt", "r") as f:
    lines = f.readlines()
    for index in range(0,len(lines),2):
        if lines[index].split(',')[-1] != lines[index+1].split(',')[-1]:
            print(lines[index].split(',')[-1])
            print(lines[index+1].split(',')[-1])
            break