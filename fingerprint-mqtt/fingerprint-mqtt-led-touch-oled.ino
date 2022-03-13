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
#include "secrets.h"

#define VERSION                       "3.0"

// MQTT Settings
#define HOSTNAME                      "garagedoor-fingerprint-sensor"                  
#define AVAILABILITY_TOPIC            "garagedoor_fingerprint_sensor/available"
#define STATE_TOPIC                   "garagedoor_fingerprint_sensor/state"        //Publish state changes to this topic
#define ATTR_TOPIC                    "garagedoor_fingerprint_sensor/attributes"   //Publish additional attributes to this topic
#define REQUEST_TOPIC                 "garagedoor_fingerprint_sensor/request"      //Listen here for requests, such as a request to enroll
#define NOTIFY_TOPIC                  "garagedoor_fingerprint_sensor/notify"       //Listen here for text to display on the OLED, such as an automation notification
#define WIFI_TOPIC                    "garagedoor_fingerprint_sensor/wifi"       

// Fingerprint Sensor
#define SENSOR_TX 12                  //GPIO Pin for WEMOS RX, SENSOR TX
#define SENSOR_RX 14                  //GPIO Pin for WEMOS TX, SENSOR RX
#define SENSOR_TOUT 0                //GPIO Pin for SENSOR T_OUT

// OLED Settings
#define OLED_RESET -1
#define SCREENSAVER_ON_DELAY 120      // Idle time before screensaver (in seconds)
#define SCREENSAVER_REFRESH 15        // Refresh time for the screensaver (in seconds)

#define REFRESH_WIFI_SIGNAL 60        //OLED Update Wifi Signal + Publish Wifi Signal (in seconds)

// OLED Variables
unsigned long millis_last_scan;       // Stores time of last scan (for screensaver)
unsigned long millis_last_refresh;    // Stores time of last refresh (for screensaver)

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

// FP variables
int id = 0;                           // Stores the current fingerprint ID
int confidenceScore = 0;              // Stores the current confidence score

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

SoftwareSerial mySerial(SENSOR_TX, SENSOR_RX);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

WiFiClient wifiClient;                // Initiate WiFi library
PubSubClient client(wifiClient);      // Initiate PubSubClient library
bool wifiState = false;               // Stores wifi connectivity state for OLED
bool mqttState = false;               // Stores mqtt connectivity state for OLED
bool ledOnOverwritten = false;
bool ledOn = false;
long lastReconnectAttempt = 0;
int wifiRssi = 0;
long lastWifiPublish = 0;
bool screensaverRunning = false;


//Declare JSON variables
DynamicJsonDocument currentAttributes(200);
char mqttBuffer[200];

void setup(){
  lastReconnectAttempt = 0;
  ledOnOverwritten = false;
  ledOn = false;
  wifiRssi = 0;
  lastWifiPublish = 0;
  screensaverRunning = false;
  
  // prep OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);

  pinMode(SENSOR_TOUT, INPUT);

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
  WiFi.hostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  oledUpdateHeader("WIFI:TRYING", false, false);
  while (WiFi.status() != WL_CONNECTED) {       // Wait till Wifi connected
    delay(1000);
  }
  wifiState = true;
  oledUpdateHeader("WIFI:CONNECTED", wifiState, false);
  delay(500);
  wifiRssi = getWifiQuality();

// OTA
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
  ArduinoOTA.setHostname(WIFI_HOSTNAME);
  ArduinoOTA.begin();

  // connect to mqtt server
  client.setServer(MQTT_SERVER, 1883);                  // Set MQTT server and port number
  client.setCallback(callback);
  oledUpdateHeader("MQTT:TRYING", wifiState, false);
  delay(500);
  reconnect();                                          //Connect to MQTT server
  oledUpdateHeader("MQTT:CONNECTED", wifiState, mqttState);
  delay(500);
  // set initial state
  oledUpdateHeader("STARTUP OK!", wifiState, mqttState);
  publishState("idle");
  delay(500);
  oledUpdateWifiPerc();
  delay(5000);                                                            // This delay to give the network a chance to settle, otherwise screen will show a brief reconnect.
}




void loop() {
  ArduinoOTA.handle();
  if (WiFi.status() != WL_CONNECTED || !client.connected()){              // if wifi or mqtt are disconnected...
    clearAllButHeader();
    if(WiFi.status() != WL_CONNECTED)
    {
      while (WiFi.status() != WL_CONNECTED) {                               // if it was wifi...wait for connection
        oledUpdateHeader("WIFI:TRYING", false, false);                      
        delay(500);
      }
      wifiState = true;
      oledUpdateHeader("WIFI:CONNECTED", wifiState, false);
      delay(500);
    }

    if (!client.connected()) {
        long millisNow = millis();
        if (millisNow - lastReconnectAttempt > 60000) {
          oledUpdateHeader("MQTT:TRYING", wifiState, false);
          lastReconnectAttempt = millisNow;
          // Attempt to reconnect
          if (reconnect()) {
            lastReconnectAttempt = 0;
            oledUpdateHeader("MQTT:CONNECTED", wifiState, mqttState);
            delay(500);
          }
          else
          {
            char state[2];
            char buf[30];
            itoa (client.state(),state,10);
            strcpy(buf,"MQTT:RC=");
            strcat(buf,state);
            oledUpdateHeader(buf, wifiState, false);
            delay(1000);
          }
       }
    }
    
  }
  if(wifiState && mqttState)
  { 
    if(millis() > millis_last_scan + SCREENSAVER_ON_DELAY*1000){            // been idle long enough for the screensaver?
      if (millis() > millis_last_refresh + SCREENSAVER_REFRESH*1000){       // and if so should we refresh the screen image?
        oledRefreshScreensaver();                                           // refresh the screensaver
        millis_last_refresh = millis();                                     // reset refresh timer
      }
    } else {
     //String readyText = "READY (v";
     //String readyText2 = ")";
     //readyText = readyText + VERSION + readyText2;
     oledUpdateHeader("READY", wifiState, mqttState);
     clearAllButHeader();
    }
    
    int fingerState = digitalRead(SENSOR_TOUT);                             // read T-Out, normally HIGH (when no finger)
    if (fingerState == HIGH) {                                              // if no finger turn off the LED
      fingerprintLed(false);
    } else {                                                                // otherwise something is present...
      oledUpdateHeader("HOLD STILL", wifiState, mqttState);
      oledTextSwipeUp("SCANNING",2,18,64,30,10); 
      delay(250);                                                           // this delay plus the animation above gives the finger time to land and settle for a good read
      uint8_t result = getFingerprintID();                                  // read fingerprint
      delay(500);                                                           // this delay is purely for the OLED UI
      if (result == FINGERPRINT_OK) {                                       // if matched...
        publishAttr("matched",id,confidenceScore);
        publishState("matched");
        oledUpdateHeader("SENDING", wifiState, mqttState);
        oledTextSwipeDown("MATCHED",2,25,30,64,10);
        delay(500);
      } else if (result == FINGERPRINT_NOTFOUND) {                          // if not matched...
        publishAttr("not matched",0,0);
        publishState("not matched");
        oledUpdateHeader("SENDING", wifiState, mqttState);
        oledTextSwipeDown("CHECKING",2,16,30,64,10);
        delay(500);
      } else {                                                              // in any other case...
        publishAttr("bad scan",0,0);
        publishState("bad scan");
        oledTextSwipeDown("CHECKING",2,16,30,64,10);
      }
      publishState("idle");
      millis_last_scan = millis();
    }

    long wifiMillisNow = millis();
    if (wifiMillisNow - lastWifiPublish > (REFRESH_WIFI_SIGNAL*1000)) {
      wifiRssi = getWifiQuality();
      lastWifiPublish = wifiMillisNow;
      publishWifi();
      oledUpdateWifiPerc();
    }
  }

  // and now wait for 1s, regularly calling the mqtt loop function
  for (int i=0; i<100; i++) {
    client.loop();
    delay(1);
  }
}




int getWifiQuality()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return -1;
    }
    int dBm = WiFi.RSSI();
    if (dBm <= -100)
    {
        return 0;
    }
    if (dBm >= -50)
    {
        return 100;
    }
    return 2 * (dBm + 100);
}





///////////////////////////////////////////////////////////////////////
//FINGERPRINT SENSOR FUNCTIONS/////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

uint8_t getFingerprintID() {
  fingerprintLed(true);
  oledTextSwipeLeft("SCANNING",18,30,"CHECKING",16,30,15);
  uint8_t p = finger.getImage();
  switch (p) {
    case FINGERPRINT_OK:
      break;
    default:
      fingerprintLed(false);
      oledUpdateHeader("IMAGE FAIL", wifiState, mqttState);
      oledShake("CHECKING",16,30);
      return p;
  }
  // image taken
  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK:
      break;
    default:
      fingerprintLed(false);
      oledUpdateHeader("CONVERT FAIL", wifiState, mqttState);
      oledShake("CHECKING",16,30);
      return p;
  }
  // converted
  p = finger.fingerFastSearch();
  switch (p) {
    case FINGERPRINT_OK:
      fingerprintLed(false);
      id = finger.fingerID;
      confidenceScore = finger.confidence;
      oledTextSwipeLeft("CHECKING",16,30,"MATCHED",25,30,15);
      return p;
    case FINGERPRINT_NOTFOUND:
      fingerprintLed(false);
      oledUpdateHeader("UNAUTHORIZED", wifiState, mqttState);
      oledShake("CHECKING",16,30);
      return p;
    default:
      fingerprintLed(false);
      oledUpdateHeader("SEARCH FAIL", wifiState, mqttState);
      oledShake("CHECKING",16,30);
      return p;
  }
}

bool getFingerprintEnroll(int enrollID) {  
  // function variables
  int p = -1;
  bool fingerPlaced = !digitalRead(SENSOR_TOUT);  
  fingerprintLed(false);
  oledTextSwipeUp("PLACE",2,35,64,30,10); // instruct user to place finger
  
  // now we loop twice and save 2 print images 
  for(uint8_t i=1; i<=2; i++){      
    
    // next wait for a finger to land...
    while (!fingerPlaced){  
      fingerPlaced = !digitalRead(SENSOR_TOUT);
      delay(100); //delay loop as full speed reading will cause the wemos to crash
    }
    // something landed...      
    fingerprintLed(true);
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
          oledUpdateHeader("IMAGE MESSY", wifiState, mqttState);
        case FINGERPRINT_PACKETRECIEVEERR:
          oledUpdateHeader("COMM ERROR", wifiState, mqttState);
        case FINGERPRINT_FEATUREFAIL:
          oledUpdateHeader("FEATURE FAIL", wifiState, mqttState);
        case FINGERPRINT_INVALIDIMAGE:
          oledUpdateHeader("BAD IMAGE", wifiState, mqttState);
        default:
          oledUpdateHeader("UNKNOWN ERROR", wifiState, mqttState);
      }
      oledShake("SCANNING",18,30);
      oledTextSwipeDown("SCANNING",2,18,30,64,10);
      return false;
    }
    // instruct user to remove finger and wait until they do
    fingerprintLed(false);
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
        oledUpdateHeader("COMM ERROR", wifiState, mqttState);
      case FINGERPRINT_ENROLLMISMATCH:
        oledUpdateHeader("BAD MATCH", wifiState, mqttState);
      default:
        oledUpdateHeader("UNKNOWN ERROR", wifiState, mqttState);
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
        oledUpdateHeader("COMM ERROR", wifiState, mqttState);
      case FINGERPRINT_BADLOCATION:
        oledUpdateHeader("LOC ERROR", wifiState, mqttState);
      case FINGERPRINT_FLASHERR:
        oledUpdateHeader("FLASH ERROR", wifiState, mqttState);
      default:
        oledUpdateHeader("UNKNOWN ERROR", wifiState, mqttState);
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
        oledUpdateHeader("COMM ERROR", wifiState, mqttState);
      case FINGERPRINT_BADLOCATION:
        oledUpdateHeader("BAD LOCATION", wifiState, mqttState);
      case FINGERPRINT_FLASHERR:
        oledUpdateHeader("FLASH ERROR", wifiState, mqttState);
      default:
        oledUpdateHeader("UNKNOWN ERROR", wifiState, mqttState);
    }
    delay(1000);
    return false;
  }
}
void fingerprintLed(bool turnOn)
{
  if(turnOn == ledOn || (ledOnOverwritten == true && turnOn == false))
  {
    return;
  }
  finger.LEDcontrol(turnOn);
  ledOn = turnOn;
}







///////////////////////////////////////////////////////////////////////
//MQTT FUNCTIONS///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

bool reconnect() {
  if(!client.connected())
  {    
    randomSeed(micros());
    
    String clientId = HOSTNAME;
    clientId += "-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, AVAILABILITY_TOPIC, 2, true, "offline")) {       //Connect to MQTT server
      client.publish(AVAILABILITY_TOPIC, "online", true);         // Once connected, publish online to the availability topic
      client.subscribe(REQUEST_TOPIC);
      client.subscribe(NOTIFY_TOPIC);
    }
  }
  mqttState = client.connected();
  return mqttState;
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
    const int capacity = JSON_OBJECT_SIZE(4);
    StaticJsonDocument<capacity> doc;
    DeserializationError error = deserializeJson(doc, payloadChar);
    if (error) {
        const char* m1 = "deserialJson failed";
        char messageTxt1[20];  
        for (i=0; i<20; i++){ 
          messageTxt1[i] = m1[i];
        }
        const char* m2 = error.c_str(); 
        char messageTxt2[20];  
        for (i=0; i<20; i++){ 
          messageTxt2[i] = m2[i];
        }
        oledUpdateHeader("ERROR", wifiState, mqttState);
        oled2TextSwipeUp(messageTxt1, messageTxt2,1,oledGetX(messageTxt1, 5, 1, 128), oledGetX(messageTxt2, 5, 1, 128),64,30,10);
        delay(5000);
        return;
    }
    const char* requestVal = doc["request"];
    const char* idChar = doc["id"];
    const char* nameVal = doc["name"];
    const char* cmdVal = doc["cmd"];
    const uint8_t id = atoi(doc["id"]);
    //if learning...
    if (strcmp(requestVal, "learn") == 0) {
      if (id > 0 && id < 128) {
        char h1[16] = "LEARNING ID ";
        strcat(h1,idChar);
        bool enrolled = false;
        while (enrolled != true){
          display.clearDisplay();
          display.display();
          oledUpdateHeader( h1, wifiState, mqttState);
          publishAttr("learning", id, 0);
          publishState("learning");
          enrolled = getFingerprintEnroll(id);  //stay inside enroll routine until returns true (learn success)
        }
        oledUpdateHeader("SUCCESS!", wifiState, mqttState);
        oledTextSwipeLeft("REMOVE",30,30,"STORED",30,30,15);
        delay(2000);
        oledTextSwipeDown("STORED",2,30,30,64,10);
        publishAttr("learned", id, 0);
        publishState("learned");
      } else {
        oledUpdateHeader("INVALID ID", wifiState, mqttState);
        delay(2000);
      }
    }
    // if deleting...
    if (strcmp(requestVal, "delete") == 0) {
      oledUpdateHeader("DELETE MODE", wifiState, mqttState);
      if (id > 0 && id < 128) {
        char h1[16] = "DELETING ID ";
        strcat(h1,idChar);
        display.clearDisplay();
        display.display();
        oledUpdateHeader( h1, wifiState, mqttState);
        publishAttr("deleting", id, 0);
        publishState("deleting");
        delay(2000);
        while (!deleteFingerprint(id));
        oledUpdateHeader("DELETED", true, true);
        publishAttr("deleted", id, 0);
        publishState("deleted");
        delay(2000);
      } else {
        oledUpdateHeader("INVALID ID", wifiState, mqttState);
        delay(2000);
      }
    }
    // if led...
    if (strcmp(requestVal, "led") == 0) {  
      if (strcmp(cmdVal, "on") == 0){
        fingerprintLed(true);
        ledOnOverwritten = true;
      } else if (strcmp(cmdVal, "off") == 0) { 
        ledOnOverwritten = false;
        fingerprintLed(false);
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
    char headerTxt[20];  //limit the header text to 20 characters
    for (i=0; i<20; i++){ //this loop to move char pointer out as can't cast directly to oledUpdateHeader
      headerTxt[i] = h[i];
    }
    const char* m = doc["message"];  //limit the header text to 16 characters
    char messageTxt[20];  //limit the message text to 20 characters
    for (i=0; i<20; i++){ //this next loop to move char pointer out as can't cast to oledUpdateHeader
      messageTxt[i] = m[i];
    }
    //then display it
    oledUpdateHeader("NOTIFICATION", wifiState, mqttState);
    oled2TextSwipeUp(headerTxt, messageTxt,1,oledGetX(headerTxt, 5, 1, 128), oledGetX(messageTxt, 5, 1, 128),64,30,10);
    delay(5000);
  }
  publishState("idle");
}



void publishState(char* state){
//  mqttMessage["state"] = state;
//  size_t mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
  if(client.connected())
  {
    client.publish(STATE_TOPIC, state);
  }
}
void publishAttr(char* state,int id, int conf){
  if(client.connected())
  {
    currentAttributes["last_state"] = state;
    currentAttributes["last_id"] = id;
    currentAttributes["last_confidence"] = conf;
    currentAttributes["software_version"] = VERSION;
    //currentAttributes["wifi_signal"] = wifiRssi;
    size_t mqttMessageSize = serializeJson(currentAttributes, mqttBuffer);
    client.publish(ATTR_TOPIC, mqttBuffer, mqttMessageSize);
  }
}
void publishWifiAttr()
{
  if(client.connected())
  {
    currentAttributes["wifi_signal"] = wifiRssi;
    size_t mqttMessageSize = serializeJson(currentAttributes, mqttBuffer);
    client.publish(ATTR_TOPIC, mqttBuffer, mqttMessageSize);
  }
}

void publishWifi()
{
  if(client.connected())
  {
    char cRssi[3];
    String sRssi = String(wifiRssi);
    sRssi.toCharArray(cRssi, 3);
    client.publish(WIFI_TOPIC, cRssi);
  }
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
    display.print(textStr);
  }

  if(mqtt){
    display.fillRect(78, 0, 12, 12, SSD1306_BLACK);
    display.drawBitmap(78, 0, mqtt_conn_ico, 12, 12, WHITE);
  }else{
    display.fillRect(78, 0, 12, 12, SSD1306_BLACK);
    display.drawBitmap(78, 0, mqtt_disc_ico, 12, 12, WHITE);
  }
  
  if(wifi){
    display.fillRect(94, 0, 12, 12, SSD1306_BLACK);
    display.drawBitmap(94, 0, wifi_conn_ico, 12, 12, WHITE);
  }else{
    display.fillRect(94, 0, 12, 12, SSD1306_BLACK);
    display.drawBitmap(94, 0, wifi_disc_ico, 12, 12, WHITE);
  }
}
void oledUpdateWifiPerc()
{
  if(!screensaverRunning)
  {
    String sWifiRssi = String(wifiRssi);
    String wifiText = sWifiRssi + "%";
    display.fillRect(110, 0, 18, 12, SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(110, 3);
    display.println(wifiText);
    display.display();
  }
}
void oledTextSwipeUp(char* textStr, int textSize, int x, int yStart, int yEnd, int steps) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  for(int16_t i=yStart; i>=yEnd; i=i-steps) {
    if(i-steps < yEnd){i = yEnd;} //enforce i ends at y
    clearAllButHeader();
    display.setCursor(x, i);
    display.println(textStr);
    display.display();
  }
}

void oled2TextSwipeUp(char* textStr1, char* textStr2, int textSize, int x1, int x2, int yStart, int yEnd, int steps) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  for(int16_t i=yStart; i>=yEnd; i=i-steps) {
    if(i-steps < yEnd){i = yEnd;} //enforce i ends at y
    clearAllButHeader();
    display.setCursor(x1, i);
    display.println(textStr1);
    display.setCursor(x2, i + 10);
    display.println(textStr2);
    display.display();
  }
}

void oledTextSwipeDown(char* textStr, int textSize, int x, int yStart, int yEnd, int steps) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  for(int16_t i = 0; i <= yEnd; i = i + steps) {
    clearAllButHeader();
    display.setCursor(x, yStart + i);
    display.println(textStr);
    display.display();
  }
}
void oledTextSwipeLeft(char* textStr1, int xStart, int yStart, char* textStr2, int xEnd, int yEnd, int steps) {
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);
  for(int16_t i = 150; i >= xEnd; i = i - steps) {
    if(i-steps < xEnd){i = xEnd;} //enforce i ends at x
    clearAllButHeader();
    display.setCursor(xStart-(150-i), yStart);
    display.println(textStr1);
    display.setCursor(i, yEnd);
    display.println(textStr2);
    display.display();
  }
}
void oledShake(char* textStr, int xStart, int yStart) {
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);
  int s=3; //shake dist
  for(int16_t i=0; i<=10; i++) {
    clearAllButHeader();
    if(i % 2 == 0) {
      display.setCursor(xStart + s, yStart);
    } else {
      display.setCursor(xStart - s, yStart);
    }
    display.println(textStr);
    display.display();
    clearAllButHeader();
    display.setCursor(xStart, yStart);
    display.println(textStr);
    display.display();
  }
}
void oledRefreshScreensaver(){
  screensaverRunning = true;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(random(0,98),random(0,54));
  display.println("zZZz");
  display.display();
}

void clearAllButHeader()
{
  display.fillRect(0, 13, 128, 51, SSD1306_BLACK); //clear all but header
}
