import meshtastic
import meshtastic.serial_interface

# By default will try to find a meshtastic device,
# otherwise provide a device path like /dev/ttyUSB0
# interface = meshtastic.serial_interface.SerialInterface()
# or something like this
interface = meshtastic.serial_interface.SerialInterface(devPath='COM8')

# or sendData to send binary data, see documentations for other options.
interface.sendText("hello mesh")

while True:
    continue