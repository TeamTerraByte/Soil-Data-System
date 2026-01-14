@echo on
setlocal

meshtastic --info
@REM meshtastic --port COM14 --info        # Set COM port if necessary

@REM --- Read PSK from file ---
set /p PSK=<psk-secret.txt

@REM --- Encrypted primary channel --- (use random to generate PSK)
@REM meshtastic --ch-set psk random --ch-index 0 
meshtastic --ch-set psk %PSK% --ch-index 0
meshtastic --ch-set name "private-uart" --ch-index 0

@REM --- Serial module configuration ---
meshtastic --set serial.enabled true
@REM Setting TEXTMSG mode seems to fail often
meshtastic --set serial.mode TEXTMSG  
meshtastic --set serial.baud 38400

@REM --- UART pins (as provided) ---
meshtastic --set serial.txd 43
@REM setting RXD seems to fail often
meshtastic --set serial.rxd 44

@REM --- Optional debugging ---
meshtastic --set serial.echo true

@REM --- Apply settings ---
meshtastic --reboot

endlocal
