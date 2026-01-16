@echo on
setlocal

@REM meshtastic --info
@REM meshtastic --port COM14 --info        # Set COM port if necessary

@REM --- Read PSK from file ---
set /p PSK=<psk-secret.txt

meshtastic --set lora.region US

@REM --- Encrypted primary channel --- (use random to generate PSK)
@REM meshtastic --ch-set psk random --ch-index 0 
meshtastic --ch-set psk base64:%PSK% --ch-index 0

meshtastic --ch-index 0 --ch-set name "RootSense"

@REM --- Serial module configuration ---
meshtastic --set serial.enabled true
TIMEOUT /T 10
@REM Setting TEXTMSG mode seems to fail often
meshtastic --set serial.mode TEXTMSG  
meshtastic --set serial.baud 38400

@REM --- UART pins (as provided) ---
meshtastic --set serial.txd 43
TIMEOUT /T 10
@REM setting RXD seems to fail often
meshtastic --set serial.rxd 44

@REM --- Optional debugging ---
meshtastic --set serial.echo true


TIMEOUT /T 10
@REM --- Apply settings ---
meshtastic --reboot

meshtastic --info

endlocal
