#!/usr/bin/env python3
import meshtastic
import meshtastic.serial_interface
from datetime import datetime
import json
from pubsub import pub

def on_receive(packet, interface):
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    
    # Extract message details
    from_id = packet.get('fromId', 'Unknown')
    to_id = packet.get('toId', 'Unknown')
    message = ''
    
    if 'decoded' in packet:
        decoded = packet['decoded']
        if 'text' in decoded:
            message = decoded['text']
    
    log_entry = {
        'timestamp': timestamp,
        'from': from_id,
        'to': to_id,
        'message': message,
        'raw': str(packet)
    }
    
    # Log to file
    with open('meshtastic_log.txt', 'a', encoding='utf-8') as f:
        f.write(f"{timestamp} | From: {from_id} | To: {to_id} | {message}\n")
    
    # Also log full JSON for detailed records
    with open('meshtastic_log.json', 'a', encoding='utf-8') as f:
        f.write(json.dumps(log_entry) + '\n')
    
    print(f"[{timestamp}] {from_id}: {message}")

# Subscribe to receive messages
pub.subscribe(on_receive, "meshtastic.receive")

# Connect via serial/USB
print("Connecting to Meshtastic device...")
interface = meshtastic.serial_interface.SerialInterface()

print("Listening for Meshtastic messages... Press Ctrl+C to stop")

# Keep the script running
try:
    while True:
        pass
except KeyboardInterrupt:
    print("\nStopping logger...")
    interface.close()