from datetime import datetime
import csv
import sys

def parse_timestamp(ts: str) -> datetime:
    """
    Convert a timestamp like '01/22/2026, 14:30:50'
    into a Python datetime object.
    """
    return datetime.strptime(ts, "%m/%d/%Y, %H:%M:%S")


# ---- Command-line argument handling ----
if len(sys.argv) != 2:
    print(f"Usage: python {sys.argv[0]} <input_csv>")
    sys.exit(1)

csv_filename = sys.argv[1]
# ---------------------------------------


sensors = {
    "hub": [],
    "worker1": [],
    "worker2": []
}

with open(csv_filename, newline="", encoding="utf-8") as file:
    reader = csv.DictReader(file)
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
    first = True  # skip first row of each sensor

    for row in sensors[sensor]:
        thisTime = parse_timestamp(row["Created (Chicago)"])
        delta = thisTime - lastTime
        lastTime = thisTime

        if first:
            first = False
            continue

        if delta.total_seconds() > 3900:  # 65 minutes
            print(
                "Gap detected at row entry",
                row["Entry ID"],
                "with a gap of",
                delta,
                "for sensor",
                sensor,
                "at time",
                row["Created (Chicago)"],
                "with",
                row["Battery (field4)"]
            )
            total_gaps += 1


print("Scan complete. Detected", total_gaps, "gaps.")

# Debug printing
# for sensor in sensors.keys():
#     print(sensor)
#     for row in sensors[sensor]:
#         print(row)
#     print("=" * 126)
