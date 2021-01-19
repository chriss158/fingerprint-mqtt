# fingerprint-mqtt.ino

This is the default sketch and is the one used in the guide. You won't need to manually change anything to run this and only need to install the libraries mentioned in the root README file.

# fingerprint-mqtt-led.ino

This version is a slight variation on the default sketch but allows you to enable and disable the onboard fingerprint LED light via an MQTT topic. I have Home Assistant turn the sensor on/off when motion is detected in the same room. You're able to send on or off to the /fingerprint/enabled topic to enable/disable the sensor. You will however need the LED on in order to take a fingerprint reading.

Requires Adafruit Fingerprint Sensor Library v2.0.4 or better. Tested with v2.0.4.

Note I've only tested this with the sensor in the guide.

# fingerprint-mqtt-led-touch.ino

Same as fingerprint-mqtt-led.ino, but in addition implements module's built-in touch sensor to only turn on LED when a finger touches the glass. Module can still be disabled (regardless of finger detection) using the MQTT topic.

**Note**: You must wire pins 4 and 5 of the FPM10A for this to work. Wire T-Out to D3 and T-3v to 3v.

# fingerprint-mqtt-led-touch-oled.ino

Same as fingerprint-mqtt-led-touch.ino, but in addition implements OLED functionality, and OTA updating (as this model lives inside a wall, and not easily accessible).  Note the following important changes!

- MQTT structure differs from other methods:
  - The sensor publishes the following:
    - STATE_TOPIC: the state in plain text (idle, matched, not matched, bad scan, learning, learned, deleting, deleted). Example: "idle".
    - ATTR_TOPIC: additional info as a JSON payload, with keys "last_state", "last_id", "last_confidence". These are useful as after a successful scan, the STATE_TOPIC will recieve a "matched" payload, however after a time this will also recieve an "idle".  The ATTR_TOPIC is useful for retaining this data for use in automations, scripts, etc. Example: {"last_state":"matched","last_id":1,"last_confidence":149}.
  - The sensor is subscribed to the following:
    - REQUEST_TOPIC: recieves requests to the sensor as a JSON payload, with keys "request", "id", "name". Accepted requests are "learn" and "delete". Example: {"request": "learn", "id": "1", "name": "Ian (Thumb)"}
    - NOTIFY_TOPIC: recieves notifications to display on the OLED. These are useful as feedback to see on the OLED that your automation has run successfully.
    
- You'll need to add in the appropriate libraries for the OLED...see https://everythingsmarthome.co.uk/esp8266/adding-an-ssd1306-oled-display-to-any-project/

**Note**: You must wire pins 4 and 5 of the FPM10A for this to work.
 

