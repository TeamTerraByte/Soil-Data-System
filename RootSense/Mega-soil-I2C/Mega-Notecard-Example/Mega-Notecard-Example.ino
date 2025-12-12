#include <Arduino.h>
#include <Notecard.h>

#define usbSerial Serial
#define productUID "edu.tamu.ag.jacob.poland:rootsense"

Notecard notecard;


// the setup function runs once when you press reset or power the board
void setup()
{
  delay(2500);
  usbSerial.begin(9600);

  notecard.begin();
  notecard.setDebugOutputStream(usbSerial);

  {
    J *req = notecard.newRequest("hub.set");
    if (req != NULL) {
      JAddStringToObject(req, "product", productUID);
      JAddStringToObject(req, "mode", "continuous");
      notecard.sendRequest(req);
    }
  }

  {
    J *req = notecard.newRequest("note.add");
    if (req != NULL) {
      JAddStringToObject(req, "file", "sensors.qo");
      JAddBoolToObject(req, "sync", true);
      J *body = JAddObjectToObject(req, "body");
      if (body) {
        JAddNumberToObject(body, "temp", 69.0);
        JAddNumberToObject(body, "humidity", 42.0);
      }
      notecard.sendRequest(req);
    }
  }

  usbSerial.println("Setup function complete");
}

// the loop function runs over and over again forever
void loop()
{
  
          // wait for a second
}
