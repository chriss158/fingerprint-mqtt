#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_Fingerprint.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

// Wifi Settings
#define SSID                          "Your Wifi SSID Here"
#define PASSWORD                      "Your Wifi Password Here"

// MQTT Settings
#define HOSTNAME                      "fingerprint-sensor-1"
#define MQTT_SERVER                   "Your MQTT Server here"														
#define STATE_TOPIC                   "fingerprint_sensor/1/state"        //Publish state changes to this topic
#define LAST_TOPIC                    "fingerprint_sensor/1/last"         //Publish last results to this topic
#define REQUEST_TOPIC                 "fingerprint_sensor/1/request"      //Listen here for requests, such as a request to enroll
#define NOTIFY_TOPIC                   "fingerprint_sensor/1/notify"       //Listen here for text to display on the OLED, such as an automation notification
#define AVAILABILITY_TOPIC            "fingerprint_sensor/1/available"
#define mqtt_username                 "Your MQTT Username"
#define mqtt_password                 "Your MQTT Password"

// Fingerprint Sensor
#define SENSOR_TX 13                  //GPIO Pin for WEMOS RX, SENSOR TX
#define SENSOR_RX 12                  //GPIO Pin for WEMOS TX, SENSOR RX
#define SENSOR_TOUT 14                //GPIO Pin for SENSOR T_OUT

// OLED
#define OLED_RESET -1

// OLED Bitmaps
const unsigned char wifi_conn_ico [] PROGMEM = {
// 'wifi', 12x12px
0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x60, 0x60, 0x00, 0x00, 0x1f, 0x80, 0x10, 0x80, 0x00, 0x00, 
0x06, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00
};
const unsigned char wifi_disc_ico [] PROGMEM = {
// 'wifi-off', 12x12px
0x00, 0x00, 0x00, 0x00, 0x5f, 0xc0, 0x60, 0x60, 0x10, 0x00, 0x1b, 0x80, 0x14, 0x80, 0x06, 0x00, 
0x07, 0x00, 0x06, 0x80, 0x00, 0x00, 0x00, 0x00
};
const unsigned char mqtt_conn_ico [] PROGMEM = {
// 'cloud-outline', 12x12px
0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x11, 0x80, 0x70, 0x80, 0xc0, 0x60, 0x80, 0x10, 0x80, 0x10, 
0xc0, 0x10, 0x7f, 0xe0, 0x00, 0x00, 0x00, 0x00
};
const unsigned char mqtt_disc_ico [] PROGMEM = {
// 'cloud-off-outline', 12x12px
0x00, 0x00, 0x00, 0x00, 0x2f, 0x00, 0x31, 0x80, 0x78, 0x80, 0xcc, 0x60, 0x86, 0x10, 0x83, 0x10, 
0xc1, 0x90, 0x7f, 0xc0, 0x00, 0x40, 0x00, 0x00
};

SoftwareSerial mySerial(SENSOR_TX, SENSOR_RX);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

WiFiClient wifiClient;                // Initiate WiFi library
PubSubClient client(wifiClient);      // Initiate PubSubClient library
bool wifiState = false;               // Stores wifi connectivity state for OLED
bool mqttState = false;               // Stores mqtt connectivity state for OLED

int id = 0;                           // Stores the current fingerprint ID
int confidenceScore = 0;              // Stores the current confidence score

unsigned long millis_last_scan;       // Stores time of last scan (for screensaver)
int screensaver_delay = 60;            // Idle time before screensaver (in seconds)

//Declare JSON variables
DynamicJsonDocument mqttMessage(200);
char mqttBuffer[200];

void setup(){
  // prep OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);

  pinMode(SENSOR_TOUT, INPUT); // Connect WEMOS 14(D5) to T-Out, T-3v to 3v

  oledUpdateHeader("SENSOR:TRYING", false, false);
  
  finger.begin(57600);
  delay(5);
  if (finger.verifyPassword()) {
    oledUpdateHeader("SENSOR:OK", false, false);
  } else {
    oledUpdateHeader("SENSOR:FAIL", false, false);
    while (1) {
      delay(1);
    }
  }

  // connect to wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  oledUpdateHeader("WIFI:TRYING", wifiState, mqttState);
  while (WiFi.status() != WL_CONNECTED) {       // Wait till Wifi connected
    delay(1000);
  }
  wifiState = true;
  oledUpdateHeader("WIFI:CONNECTED", wifiState, mqttState);
  delay(500);

  // OTA prep
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    oledUpdateHeader("FLASHING...", wifiState, mqttState);
  });
  ArduinoOTA.onEnd([]() {
    oledUpdateHeader("FLASH COMPLETE", wifiState, mqttState);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      oledUpdateHeader("AUTH FAIL", wifiState, mqttState);
    } else if (error == OTA_BEGIN_ERROR) {
      oledUpdateHeader("BEGIN FAIL", wifiState, mqttState);
    } else if (error == OTA_CONNECT_ERROR) {
      oledUpdateHeader("CONNECT FAIL", wifiState, mqttState);
    } else if (error == OTA_RECEIVE_ERROR) {
      oledUpdateHeader("RECIEVE FAIL", wifiState, mqttState);
    } else if (error == OTA_END_ERROR) {
      oledUpdateHeader("END FAIL", wifiState, mqttState);
    }
  });
  ArduinoOTA.begin();

  // connect to mqtt server
  client.setServer(MQTT_SERVER, 1883);                  // Set MQTT server and port number
  client.setCallback(callback);
  oledUpdateHeader("MQTT:TRYING", wifiState, mqttState);
  delay(500);
  reconnect();                                          //Connect to MQTT server
  oledUpdateHeader("MQTT:CONNECTED", wifiState, mqttState);
  delay(500);
  // set initial state
  oledUpdateHeader("READY", wifiState, mqttState);
  publishState("idle",0,0);
  delay(500);
}




void loop() {
  ArduinoOTA.handle();
  if (WiFi.status() != WL_CONNECTED || !client.connected()){
    while (WiFi.status() != WL_CONNECTED) {
      oledUpdateHeader("WIFI:TRYING", false, false);
      delay(500);
    }
    oledUpdateHeader("WIFI:CONNECTED", true, false);
    delay(500);
    if (!client.connected()) {
      oledUpdateHeader("MQTT:TRYING", true, false);
      reconnect();                //Just incase we get disconnected from MQTT server
      oledUpdateHeader("MQTT:CONNECTED", true, true);
      delay(500);
    }
  }
  oledUpdateHeader("READY", true, true);

  int fingerState = digitalRead(SENSOR_TOUT); // Read T-Out, normally HIGH (when no finger)
  if (fingerState == HIGH) {
    finger.LEDcontrol(false);
  } else {
    oledUpdateHeader("HOLD STILL", true, true);
    oledTextSwipeUp("SCANNING",2,18,64,30,10); 
    delay(250); //this delay plus the animation above gives the finger time to land and settle for a good read
    uint8_t result = getFingerprintID();
    delay(500); //this delay is purely for the OLED UI
    if (result == FINGERPRINT_OK) {
      publishState("matched",id,confidenceScore);
      publishLast("matched",id,confidenceScore);
      oledUpdateHeader("SENDING", true, true);
      oledTextSwipeDown("MATCHED",2,25,30,64,10);
      delay(500);
    } else if (result == FINGERPRINT_NOTFOUND) {
      publishState("not matched",0,0);
      publishLast("not matched",0,0);
      oledUpdateHeader("SENDING", true, true);
      oledTextSwipeDown("CHECKING",2,16,30,64,10);
      delay(500);
    } else {
      publishState("bad scan",0,0);
      publishLast("bad scan",0,0);
      oledTextSwipeDown("CHECKING",2,16,30,64,10);
    }
    publishState("idle",0,0);
    millis_last_scan = millis();
  }
  client.loop();
  delay(100);            //don't need to run this at full speed.
  if(millis() > millis_last_scan + screensaver_delay*1000){
    oledScreensaver();
  }
}







///////////////////////////////////////////////////////////////////////
//FINGERPRINT SENSOR FUNCTIONS/////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

uint8_t getFingerprintID() {
  finger.LEDcontrol(true);
  oledTextSwipeLeft("SCANNING",18,30,"CHECKING",16,30,15);
  uint8_t p = finger.getImage();
  switch (p) {
    case FINGERPRINT_OK:
      break;
    default:
      finger.LEDcontrol(false);
      oledUpdateHeader("IMAGE FAIL", true, true);
      oledShake("CHECKING",16,30);
      return p;
  }
  // image taken
  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK:
      break;
    default:
      finger.LEDcontrol(false);
      oledUpdateHeader("CONVERT FAIL", true, true);
      oledShake("CHECKING",16,30);
      return p;
  }
  // converted
  p = finger.fingerFastSearch();
  switch (p) {
    case FINGERPRINT_OK:
      finger.LEDcontrol(false);
      id = finger.fingerID;
      confidenceScore = finger.confidence;
      oledTextSwipeLeft("CHECKING",16,30,"MATCHED",25,30,15);
      return p;
    case FINGERPRINT_NOTFOUND:
      finger.LEDcontrol(false);
      oledUpdateHeader("UNAUTHORIZED", true, true);
      oledShake("CHECKING",16,30);
      return p;
    default:
      finger.LEDcontrol(false);
      oledUpdateHeader("SEARCH FAIL", true, true);
      oledShake("CHECKING",16,30);
      return p;
  }
}

bool getFingerprintEnroll(int enrollID) {  
  // function variables
  int p = -1;
  bool fingerPlaced = !digitalRead(SENSOR_TOUT);  
  finger.LEDcontrol(false);
  oledTextSwipeUp("PLACE",2,35,64,30,10); // instruct user to place finger
  
  // now we loop twice and save 2 print images 
  for(uint8_t i=1; i<=2; i++){      
    
    // next wait for a finger to land...
    while (!fingerPlaced){  
      fingerPlaced = !digitalRead(SENSOR_TOUT);
      delay(100); //delay loop as full speed reading will cause the wemos to crash
    }
    // something landed...      
    finger.LEDcontrol(true);
    oledTextSwipeLeft("PLACE",35,30,"SCANNING",18,30,15);  // use animation delay to let the finger settle    
    // image it...
    while (finger.getImage() != FINGERPRINT_OK) {}     // loop until we get fingerprint_ok
    // got a usable image (FINGERPRINT_OK), verify its a print...
    p = finger.image2Tz(i);   
    if (p == FINGERPRINT_OK) {
      // do nothing
    } else {
      switch (p){
        case FINGERPRINT_IMAGEMESS:
          oledUpdateHeader("IMAGE MESSY", true, true);
        case FINGERPRINT_PACKETRECIEVEERR:
          oledUpdateHeader("COMM ERROR", true, true);
        case FINGERPRINT_FEATUREFAIL:
          oledUpdateHeader("FEATURE FAIL", true, true);
        case FINGERPRINT_INVALIDIMAGE:
          oledUpdateHeader("BAD IMAGE", true, true);
        default:
          oledUpdateHeader("UNKNOWN ERROR", true, true);
      }
      oledShake("SCANNING",18,30);
      oledTextSwipeDown("SCANNING",2,18,30,64,10);
      return false;
    }
    // instruct user to remove finger and wait until they do
    finger.LEDcontrol(false);
    oledTextSwipeLeft("SCANNING",18,30,"REMOVE",30,30,15);
    while (fingerPlaced){  //wait here for finger to leave
      fingerPlaced = !digitalRead(SENSOR_TOUT);
      delay(100); //delay req as full speed reading will cause the wemos to crash
    }     
    delay(250); // let the finger exit
    // if its the first read, instruct to repeat
    if(i==1){oledTextSwipeLeft("REMOVE",30,30,"PLACE",35,30,15);}
  }
  
  // have 2 good reads, try and create the model
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    // model created, continue
  } else {
    switch (p){
      case FINGERPRINT_PACKETRECIEVEERR:
        oledUpdateHeader("COMM ERROR", true, true);
      case FINGERPRINT_ENROLLMISMATCH:
        oledUpdateHeader("BAD MATCH", true, true);
      default:
        oledUpdateHeader("UNKNOWN ERROR", true, true);
    }
    oledShake("SCANNING",18,30);
    oledTextSwipeDown("SCANNING",2,18,30,64,10);
    return false;
  }

  // model created, store it
  p = finger.storeModel(enrollID);

  if (p == FINGERPRINT_OK) {
    // model stored ok, all done here.
    // instruct user to remove finger
    while (fingerPlaced){  //wait here for finger to leave
      fingerPlaced = !digitalRead(SENSOR_TOUT);
      delay(100); //delay req as full speed reading will cause the wemos to crash
    }     
    delay(250); // let the finger exit
    return true;
  } else {
    switch (p){
      case FINGERPRINT_PACKETRECIEVEERR:
        oledUpdateHeader("COMM ERROR", true, true);
      case FINGERPRINT_BADLOCATION:
        oledUpdateHeader("LOC ERROR", true, true);
      case FINGERPRINT_FLASHERR:
        oledUpdateHeader("FLASH ERROR", true, true);
      default:
        oledUpdateHeader("UNKNOWN ERROR", true, true);
    }
    oledShake("SCANNING",18,30);
    oledTextSwipeDown("SCANNING",2,18,30,64,10);
    return false;
  }
}

bool deleteFingerprint(int deleteID) {
  uint8_t p = -1;
  p = finger.deleteModel(deleteID);
  if (p == FINGERPRINT_OK) {
    return true;
  } else {
    switch (p){
      case FINGERPRINT_PACKETRECIEVEERR:
        oledUpdateHeader("COMM ERROR", true, true);
      case FINGERPRINT_BADLOCATION:
        oledUpdateHeader("BAD LOCATION", true, true);
      case FINGERPRINT_FLASHERR:
        oledUpdateHeader("FLASH ERROR", true, true);
      default:
        oledUpdateHeader("UNKNOWN ERROR", true, true);
    }
    delay(1000);
    return false;
  }
}







///////////////////////////////////////////////////////////////////////
//MQTT FUNCTIONS///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

void reconnect() {
  while (!client.connected()) {       // Loop until connected to MQTT server
    if (client.connect(HOSTNAME, mqtt_username, mqtt_password, AVAILABILITY_TOPIC, 1, true, "offline")) {       //Connect to MQTT server
      client.publish(AVAILABILITY_TOPIC, "online");         // Once connected, publish online to the availability topic
      client.subscribe(REQUEST_TOPIC);
      client.subscribe(NOTIFY_TOPIC);
      mqttState = true;
    } else {
      delay(5000);  // Will attempt connection again in 5 seconds
      mqttState = false;
    }
  }
}
void callback(char* topic, byte* payload, unsigned int length) {          //The MQTT callback which listens for incoming messages on the subscribed topics
  int i;
  oledUpdateHeader("RECIEVING", true, true);
  delay(250);
  //check incoming topic
  if (strcmp(topic, REQUEST_TOPIC) == 0){
    // first convert payload to char array
    char payloadChar[length+1];
    for (int i = 0; i < length; i++) {
      payloadChar[i] = payload[i];
    }
    // second deserialize json payload and save variables
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payloadChar);
    const char* requestVal = doc["request"];
    const char* idChar = doc["id"];
    const char* nameVal = doc["name"];
    const uint8_t id = atoi(doc["id"]);
    //if learning...
    if (strcmp(requestVal, "learn") == 0) {
      if (id > 0 && id < 128) {
        char h1[16] = "LEARNING ID ";
        strcat(h1,idChar);
        bool enrolled = false;
        while (enrolled != true){
          oledUpdateHeader( h1, true, true);
          publishState("learning", id, 0);
          publishLast("learning", id, 0);
          enrolled = getFingerprintEnroll(id);  //stay inside enroll routine until returns true (learn success)
        }
        oledUpdateHeader("SUCCESS!", true, true);
        oledTextSwipeLeft("REMOVE",30,30,"STORED",30,30,15);
        delay(2000);
        oledTextSwipeDown("STORED",2,30,30,64,10);
        publishState("learned", id, 0);
        publishLast("learned", id, 0);
      } else {
        oledUpdateHeader("INVALID ID", true, true);
        delay(2000);
      }
    }
    // if deleting...
    if (strcmp(requestVal, "delete") == 0) {
      oledUpdateHeader("DELETE MODE", true, true);
      if (id > 0 && id < 128) {
        char h1[16] = "DELETING ID ";
        strcat(h1,idChar);
        oledUpdateHeader( h1, true, true);
        publishState("deleting", id, 0);
        publishLast("deleting", id, 0);
        delay(500);
        while (!deleteFingerprint(id));
        oledUpdateHeader("DELETED", true, true);
        publishState("deleted", id, 0);
        publishLast("deleted", id, 0);
        delay(500);
      } else {
        oledUpdateHeader("INVALID ID", true, true);
        delay(2000);
      }
    }
  }
  // display the notification from the server
  if (strcmp(topic, NOTIFY_TOPIC) == 0){
    // first convert payload to char array
    char payloadChar[length+1];
    for (int i = 0; i < length; i++) {
      payloadChar[i] = payload[i];
    }
    // second deserialize json payload and save variables
    StaticJsonDocument<200> doc;
    deserializeJson(doc, payloadChar);
    const char* h = doc["header"];
    char headerTxt[16];  //limit the header text to 16 characters
    for (i=0; i<15; i++){ //this loop to move char pointer out as can't cast directly to oledUpdateHeader
      headerTxt[i] = h[i];
    }
    const char* m = doc["message"];  //limit the header text to 16 characters
    char messageTxt[16];  //limit the message text to 16 characters
    for (i=0; i<15; i++){ //this next loop to move char pointer out as can't cast to oledUpdateHeader
      messageTxt[i] = m[i];
    }
    //then display it
    oledUpdateHeader("NOTIFICATION", true, true);
    oledTextSwipeUp(messageTxt,1,oledGetX(messageTxt, 5, 1, 128),64,30,10);
    delay(1500);
    oledTextSwipeDown(messageTxt,1,oledGetX(messageTxt, 5, 1, 128),30,64,10);
  }
  publishState("idle", 0, 0);
}
void publishState(char* state,int id, int conf){
  mqttMessage["state"] = state;
  mqttMessage["id"] = id;
  mqttMessage["confidence"] = conf;
  size_t mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
  client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
}
void publishLast(char* state,int id, int conf){
  mqttMessage["state"] = state;
  mqttMessage["id"] = id;
  mqttMessage["confidence"] = conf;
  size_t mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
  client.publish(LAST_TOPIC, mqttBuffer, mqttMessageSize);
}







///////////////////////////////////////////////////////////////////////
//OLED FUNCTIONS///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

uint8_t oledGetX(char* text, uint8_t fontWidth, uint8_t spaceWidth, uint8_t oledWidth){
  //approximates the start position to center the string on the OLED display
  uint8_t charCount = strlen(text);
  uint8_t stringWidth = (fontWidth * charCount) + (spaceWidth * charCount - 1);
  return ((oledWidth - stringWidth) /2);
}

void oledUpdateHeader(char* textStr, bool wifi, bool mqtt) {
  if(textStr != "NULL"){
    display.fillRect(0, 0, 99, 12, SSD1306_BLACK);
    display.drawFastHLine(0,12,128,SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 3);
    display.println(textStr);
  }
  if(wifi){
    display.fillRect(100, 0, 12, 12, SSD1306_BLACK);
    display.drawBitmap(100, 0, wifi_conn_ico, 12, 12, WHITE);
  }else{
    display.fillRect(100, 0, 12, 12, SSD1306_BLACK);
    display.drawBitmap(100, 0, wifi_disc_ico, 12, 12, WHITE);
  }
  if(mqtt){
    display.fillRect(116, 0, 12, 12, SSD1306_BLACK);
    display.drawBitmap(116, 0, mqtt_conn_ico, 12, 12, WHITE);
  }else{
    display.fillRect(116, 0, 12, 12, SSD1306_BLACK);
    display.drawBitmap(116, 0, mqtt_disc_ico, 12, 12, WHITE);
  }
  display.display();
}
void oledTextSwipeUp(char* textStr, int textSize, int x, int yStart, int yEnd, int steps) {
  display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  for(int16_t i=yStart; i>=yEnd; i=i-steps) {
    if(i-steps < yEnd){i = yEnd;} //enforce i ends at y
    display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
    display.setCursor(x, i);
    display.println(textStr);
    display.display();
  }
}
void oledTextSwipeDown(char* textStr, int textSize, int x, int yStart, int yEnd, int steps) {
  display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  for(int16_t i = 0; i <= yEnd; i = i + steps) {
    display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
    display.setCursor(x, yStart + i);
    display.println(textStr);
    display.display();
  }
}
void oledTextSwipeLeft(char* textStr1, int xStart, int yStart, char* textStr2, int xEnd, int yEnd, int steps) {
  display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);
  for(int16_t i = 150; i >= xEnd; i = i - steps) {
    if(i-steps < xEnd){i = xEnd;} //enforce i ends at x
    display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
    display.setCursor(xStart-(150-i), yStart);
    display.println(textStr1);
    display.setCursor(i, yEnd);
    display.println(textStr2);
    display.display();
  }
}
void oledShake(char* textStr, int xStart, int yStart) {
  display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);
  int s=3; //shake dist
  for(int16_t i=0; i<=10; i++) {
    display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
    if(i % 2 == 0) {
      display.setCursor(xStart + s, yStart);
    } else {
      display.setCursor(xStart - s, yStart);
    }
    display.println(textStr);
    display.display();
    display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
    display.setCursor(xStart, yStart);
    display.println(textStr);
    display.display();
  }
}
void oledScreensaver(){
  bool fingerPlaced = false;
  unsigned int i = 10000;
  unsigned long t = 0;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // stay in this loop until a finger lands
  while (!fingerPlaced){  
    if (millis() > t){
      display.clearDisplay();
      display.setCursor(random(0,98),random(0,54));
      display.println("zZZz");
      display.display();
      t = millis() + i;
    }
    fingerPlaced = !digitalRead(SENSOR_TOUT);
    delay(100); //delay loop as full speed reading will cause the wemos to crash
  }
  display.clearDisplay();
  millis_last_scan = millis();
}
