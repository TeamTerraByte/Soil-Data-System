from datetime import datetime
import csv
import sys
import plotly.graph_objects as go

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

# Read CSV and sort rows by sensor
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


for sensor in sensors.keys():
    # Parse the data
    timestamps = []
    voltages = []

    if len(sensors[sensor]) == 0:
        print("Sensor data from", sensor, "is not present.")
        continue

    for row in sensors[sensor]:
        # Parse timestamp
        timestamp_str = row["Created (Chicago)"]
        timestamp = datetime.strptime(timestamp_str, '%m/%d/%Y, %H:%M:%S')
        timestamps.append(timestamp)
        
        # Parse voltage (remove 'Batt,' prefix and convert to float)
        voltage_str = row["Battery (field4)"]
        voltage = float(voltage_str.split(',')[1])
        voltages.append(voltage)

    # Create the plot
    fig = go.Figure()

    fig.add_trace(go.Scatter(
        x=timestamps,
        y=voltages,
        mode='lines+markers',
        name=sensor + ' Battery Voltage',
        line=dict(color='blue', width=2),
        marker=dict(size=6)
    ))

    # Update layout
    fig.update_layout(
        title='Battery Voltage of ' + sensor + ' Over Time',
        xaxis_title='Time',
        yaxis_title='Voltage (V)',
        hovermode='x unified',
        template='plotly_white'
    )

    # Show the plot
    fig.show()