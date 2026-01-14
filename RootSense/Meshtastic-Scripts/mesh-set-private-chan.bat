meshtastic --info
# meshtastic --port COM5 --info        # Set COM port if necessary

# --- Encrypted primary channel ---
meshtastic --ch-set psk random --ch-index 0
meshtastic --ch-set name "private-uart" --ch-index 0

# --- Serial module configuration ---
meshtastic --set serial.enabled true
meshtastic --set serial.mode TEXTMSG
meshtastic --set serial.baud 38400

# --- UART pins (Heltec ESP32 V3 safe pins) ---
meshtastic --set serial.txd 43
meshtastic --set serial.rxd 44

# --- Optional debugging ---
meshtastic --set serial.echo true

# --- Apply settings ---
meshtastic --reboot
