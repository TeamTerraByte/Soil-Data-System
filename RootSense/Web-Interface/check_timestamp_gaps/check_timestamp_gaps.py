from datetime import datetime
import csv

def parse_timestamp(ts: str) -> datetime:
    """
    Convert a timestamp like '01/22/2026, 14:30:50'
    into a Python datetime object.
    """
    return datetime.strptime(ts, "%m/%d/%Y, %H:%M:%S")

sensors = {
    "hub": [],
    "worker1": [],
    "worker2": []
}

with open("thingspeak_feeds_2026-01-26T19-20-52.291Z.csv", newline="", encoding="utf-8") as file:
    reader = csv.DictReader(file)
    i = 0
    for row in reader:
        if not row["Device (field1)"]:
            continue
        
        if "hub" in row["Device (field1)"]:
            sensors["hub"].append(row)
        if "@w1r" in row["Device (field1)"]:
            sensors["worker1"].append(row)
        if "@w2r" in row["Device (field1)"]: 
            sensors["worker2"].append(row)


total_gaps = 0
for sensor in sensors.keys():
    lastTime = datetime.min
    thisTime = datetime.min
    first = True  # used to skip first row of each sensor
    for row in sensors[sensor]:
        thisTime = parse_timestamp(row["Created (Chicago)"])
        delta = thisTime - lastTime
        lastTime = thisTime
        if first == True:
            first = False
            continue
        if delta.total_seconds() > 3900:  # 65 minutes
            print("Gap detected at row entry", row["Entry ID"], "with a gap of", delta)
            total_gaps += 1


print("Scan complete. Detected", total_gaps, "gaps.")