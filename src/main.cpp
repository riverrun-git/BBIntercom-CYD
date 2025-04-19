/*******************************************************************
    Template project for the ESP32 Cheap Yellow Display.

    https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display

    Written by Andreas Schneider
    Based on code by by Brian Lough and maybe others
 *******************************************************************/

// Make sure to copy the UserSetup.h file into the library as
// per the Github Instructions. The pins are defined in there.

// Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
// Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
// Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
// Note the following larger fonts are primarily numeric only!
// Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
// Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
// Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.

#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h> // Not working with my board
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <WebServer.h>
#include <PubSubClient.h> // MQTT
#include "Free_Fonts.h"
#include "constants.h"
#include "credentials.h"
#include "logo.h"

#define BACKLIGHT_PIN 21
#define INTERCOM_PIN 22

// Touchscreen pins
#define XPT2046_IRQ 36  // T_IRQ
#define XPT2046_MOSI 32 // T_DIN
#define XPT2046_MISO 39 // T_OUT
#define XPT2046_CLK 25  // T_CLK
#define XPT2046_CS 33   // T_CS

SPIClass mySPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS /*, XPT2046_IRQ*/);

TFT_eSPI tft = TFT_eSPI();

char hostname[32] = "BB Intercom";
const char *ssid = "Searching"; // displayed as a placeholder while not connected
char lastTimeReceived[32] = "--:--";

#define INTERCOM_IDLE ""
#define INTERCOM_RINGING "DING DONG"
char intercomState[32] = INTERCOM_IDLE;

#define UNCALIBRATED TS_Point(-1, -1, 0);
TS_Point touchPoint;
TS_Point calibrationTopLeft = UNCALIBRATED;
TS_Point calibrationTopRight = UNCALIBRATED;
TS_Point calibrationBottomLeft = UNCALIBRATED;
TS_Point calibrationBottomRight = UNCALIBRATED;

#define SCREEN_TIMEOUT 1000 * 30

unsigned long screenTimeout = 0;  // 0 or timestamp when last released
unsigned long touchStartTime = 0; // 0 or timestamp when pressed
unsigned long nextTouchTimer = 0; // 0 or or timestamp

#define IDLE 1
#define RINGING 0
int lastIntercomState = IDLE;
int currentIntercomState = IDLE;

Preferences preferences;
WebServer server(80);

int orientation = 1;

int launchCount = 0;
bool calibrated = false;
int calibrating = 0; // The point currently being calibrated or 0

// #define FONT_HEIGHT 30
#define FONT_NUMBER 4 // Use GFXFF to use an Adafruit free font - I like font 4
#define FREE_FONT FF6 // Only has an effect if FONT_NUMBER is set to GFXFF
#define FONT_HEIGHT 26
#define DISPLAY_LINES 8

#define HOSTNAME_LINE 1
#define SSID_LINE 2
#define IP_LINE 3
#define BROKER_TEXT_LINE 4
#define BROKER_IP_LINE 5
#define BROKER_STATUS_LINE 6
#define RESET_LINE 6
#define DING_DONG_LINE 7
#define CLOCK_LINE 8

// We can globally disable Serial comms by undefining this
#define USE_SERIAL
void print(const char *text)
{
#ifdef USE_SERIAL
  Serial.print(text);
#endif
}

void println(const char *text)
{
#ifdef USE_SERIAL
  Serial.println(text);
#endif
}

WiFiClient wifiClient;               // The Wifi connection
PubSubClient mqttClient(wifiClient); // The MQTT connection
const char *mqttBroker = "";
int mqttPort = 0;

const char *MQTT_TOPIC_TIME = "/intercom/time"; // incoming
const char *MQTT_TOPIC_INFO = "/intercom/info";
const char *MQTT_TOPIC_ALERT = "/intercom/active";

void clearDisplay()
{
  tft.fillScreen(BACKGROUND_COLOUR);
  tft.setTextColor(TEXT_COLOUR, BACKGROUND_COLOUR);
}

// Draw the calibration crosshair at TOP_LEFT, BOTTOM_RIGHT etc...
void drawCrosshair(int position)
{
  bool left = position == TOP_LEFT || position == BOTTOM_LEFT;
  bool top = position == TOP_LEFT || position == TOP_RIGHT;
  int32_t xStart = left ? CROSSHAIR_MARGIN : tft.width() - CROSSHAIR_MARGIN - CROSSHAIR_SIZE;
  int32_t xMiddle = xStart + CROSSHAIR_SIZE / 2;
  int32_t xEnd = xStart + CROSSHAIR_SIZE;
  int32_t yStart = top ? CROSSHAIR_MARGIN : tft.height() - CROSSHAIR_MARGIN - CROSSHAIR_SIZE;
  int32_t yMiddle = yStart + CROSSHAIR_SIZE / 2;
  int32_t yEnd = yStart + CROSSHAIR_SIZE;
  tft.drawLine(xStart, yMiddle, xEnd, yMiddle, CROSSHAIR_COLOUR);
  tft.drawLine(xMiddle, yStart, xMiddle, yEnd, CROSSHAIR_COLOUR);
}

void updateDisplayLine(int line, const char *text)
{
  int displayHeight = tft.height();
  int lineHeight = displayHeight / DISPLAY_LINES;
  int gap = lineHeight - FONT_HEIGHT;
  int topGap = gap / 2;
  int yTop = (line - 1) * lineHeight;
  int yText = yTop + topGap;
  tft.fillRect(0, yTop, tft.width(), lineHeight, BACKGROUND_COLOUR);
  tft.drawCentreString(text, tft.width() / 2, yText, FONT_NUMBER);
}

// display the connection info (and cross hair if calibrating)
void displayStatus()
{
  println("Display status");
  clearDisplay();
  updateDisplayLine(HOSTNAME_LINE, hostname);
  if (WiFi.status() == WL_CONNECTED)
  {
    updateDisplayLine(SSID_LINE, WiFi.SSID().c_str());
    updateDisplayLine(IP_LINE, WiFi.localIP().toString().c_str());
  }
  else
  {
    updateDisplayLine(SSID_LINE, "WiFi not connected");
    updateDisplayLine(IP_LINE, "Restart to connect");
  }
  updateDisplayLine(BROKER_TEXT_LINE, "MQTT broker:");
  if (mqttClient.connected())
  {
    char buffer[32];
    sprintf(buffer, "%s:%i", mqttBroker, mqttPort);
    updateDisplayLine(BROKER_IP_LINE, buffer);
  }
  else
  {
    updateDisplayLine(BROKER_IP_LINE, "not connected");
  }
  updateDisplayLine(DING_DONG_LINE, intercomState);
  println(lastTimeReceived);
  updateDisplayLine(CLOCK_LINE, lastTimeReceived);
  if (calibrating)
  {
    drawCrosshair(calibrating);
    updateDisplayLine(RESET_LINE, "Touch crosshairs!");
  }
}

void displayLogo()
{
  clearDisplay();
  tft.pushImage((tft.width() - LOGO_WIDTH)/2, 0, LOGO_WIDTH, LOGO_HEIGHT, logo);
}

void displayOn()
{
  digitalWrite(BACKLIGHT_PIN, HIGH);
  screenTimeout = millis() + SCREEN_TIMEOUT;
}

void displayOff()
{
  digitalWrite(BACKLIGHT_PIN, LOW);
  clearDisplay();
  screenTimeout = 0;
}

void printCalibrationInfo()
{
  char buffer[40];
  sprintf(buffer, "Cal: %i,%i %i,%i", calibrationTopLeft.x, calibrationTopLeft.y, calibrationTopRight.x, calibrationTopRight.y);
  println(buffer);
  sprintf(buffer, "Cal: %i,%i %i,%i", calibrationBottomLeft.x, calibrationBottomLeft.y, calibrationBottomRight.x, calibrationBottomRight.y);
  println(buffer);
}

void resetStoredCalibration()
{
  preferences.begin(PREFERENCES_NAMESPACE);
  preferences.putBool(PREFERENCES_KEY_CALIBRATED, false);
  preferences.putInt(PREFERENCES_KEY_TOP_LEFT_X, -1);
  preferences.putInt(PREFERENCES_KEY_TOP_LEFT_Y, -1);
  preferences.putInt(PREFERENCES_KEY_TOP_RIGHT_X, -1);
  preferences.putInt(PREFERENCES_KEY_TOP_RIGHT_Y, -1);
  preferences.putInt(PREFERENCES_KEY_BOTTOM_LEFT_X, -1);
  preferences.putInt(PREFERENCES_KEY_BOTTOM_LEFT_Y, -1);
  preferences.putInt(PREFERENCES_KEY_BOTTOM_RIGHT_X, -1);
  preferences.putInt(PREFERENCES_KEY_BOTTOM_RIGHT_Y, -1);
  preferences.end();
}

void storeCalibration()
{
  preferences.begin(PREFERENCES_NAMESPACE);
  preferences.putBool(PREFERENCES_KEY_CALIBRATED, true);
  preferences.putInt(PREFERENCES_KEY_TOP_LEFT_X, calibrationTopLeft.x);
  preferences.putInt(PREFERENCES_KEY_TOP_LEFT_Y, calibrationTopLeft.y);
  preferences.putInt(PREFERENCES_KEY_TOP_RIGHT_X, calibrationTopRight.x);
  preferences.putInt(PREFERENCES_KEY_TOP_RIGHT_Y, calibrationTopRight.y);
  preferences.putInt(PREFERENCES_KEY_BOTTOM_LEFT_X, calibrationBottomLeft.x);
  preferences.putInt(PREFERENCES_KEY_BOTTOM_LEFT_Y, calibrationBottomLeft.y);
  preferences.putInt(PREFERENCES_KEY_BOTTOM_RIGHT_X, calibrationBottomRight.x);
  preferences.putInt(PREFERENCES_KEY_BOTTOM_RIGHT_Y, calibrationBottomRight.y);
  preferences.end();
}

// Load or initialize preferences
void setupPreferences()
{
  preferences.begin(PREFERENCES_NAMESPACE);
  if (preferences.isKey(PREFERENCES_KEY_CALIBRATED))
  {
    calibrated = preferences.getBool(PREFERENCES_KEY_CALIBRATED);
    Serial.print("Calibrated: ");
    Serial.println(calibrated);
  }
  else
  {
    preferences.putBool(PREFERENCES_KEY_CALIBRATED, false);
  }
  if (preferences.isKey(PREFERENCES_KEY_TOP_LEFT_X))
  {
    // if that key exists all the other should as well
    int x = preferences.getInt(PREFERENCES_KEY_TOP_LEFT_X);
    int y = preferences.getInt(PREFERENCES_KEY_TOP_LEFT_Y);
    calibrationTopLeft = TS_Point(x, y, 0);
    x = preferences.getInt(PREFERENCES_KEY_TOP_RIGHT_X);
    y = preferences.getInt(PREFERENCES_KEY_TOP_RIGHT_Y);
    calibrationTopRight = TS_Point(x, y, 0);
    x = preferences.getInt(PREFERENCES_KEY_BOTTOM_LEFT_X);
    y = preferences.getInt(PREFERENCES_KEY_BOTTOM_LEFT_Y);
    calibrationBottomLeft = TS_Point(x, y, 0);
    x = preferences.getInt(PREFERENCES_KEY_BOTTOM_RIGHT_X);
    y = preferences.getInt(PREFERENCES_KEY_BOTTOM_RIGHT_Y);
    calibrationBottomRight = TS_Point(x, y, 0);
  }
  else
  {
    resetStoredCalibration();
    printCalibrationInfo();
  }
  printCalibrationInfo();
  preferences.end();
}

void handleTime()
{
  char buffer[20];
  sprintf(buffer, "%d", millis());
  println(buffer);
  server.send(200, "text/plain", buffer);
}

void handleColour()
{
  println("POST");
  if (server.hasArg("plain"))
  {
    String body = server.arg("plain");
    print("Arg ");
    println(body.c_str());
  }
  server.send(200, "text/plain", "Thank you.");
}
void setupRouting()
{
  server.on("/time", handleTime);
  server.on("/colour", HTTP_POST, handleColour);
}

int touchCountDown = 0;

void handleTouchTimer()
{
  if (touchCountDown == 0)
  {
    touchCountDown = 5;
  }
  else
  {
    touchCountDown -= 1;
    if (touchCountDown == 0)
    {
      resetStoredCalibration();
      ESP.restart();
    }
  }
  char message[32];
  sprintf(message, "Reset in %is", touchCountDown);
  updateDisplayLine(RESET_LINE, message);
  nextTouchTimer = millis() + 1000;
}

void handleCalibrationEvent()
{
  switch (calibrating)
  {
  case TOP_LEFT:
    calibrationTopLeft = touchPoint;
    calibrating = TOP_RIGHT;
    break;
  case TOP_RIGHT:
    calibrationTopRight = touchPoint;
    calibrating = BOTTOM_LEFT;
    break;
  case BOTTOM_LEFT:
    calibrationBottomLeft = touchPoint;
    calibrating = BOTTOM_RIGHT;
    break;
  case BOTTOM_RIGHT:
    calibrationBottomRight = touchPoint;
    calibrating = 0;
    calibrated = true;
    storeCalibration();
    printCalibrationInfo();
    break;
  }
  displayStatus();
}

void handleTouchStartEvent()
{
  char buffer[64] = "";
  if (screenTimeout == 0)
  {
    // display is off
    clearDisplay();
    displayOn();
    displayLogo();
    delay(1000);
    displayStatus();
    sprintf(buffer, "WAKE x:%d y:%d z:%d", touchPoint.x, touchPoint.y, touchPoint.z);
    println(buffer);
  }
  else if (touchStartTime == 0)
  {
    sprintf(buffer, "TOUCH x:%d y:%d z:%d", touchPoint.x, touchPoint.y, touchPoint.z);
    println(buffer);
    touchStartTime = millis();
    nextTouchTimer = millis() + 1000; // every second
    if (calibrating)
    {
      handleCalibrationEvent();
    }
  }
}

void handleTouchEndEvent()
{
  println("NO TOUCH");
  updateDisplayLine(RESET_LINE, "");
  touchStartTime = 0;
  nextTouchTimer = 0;
  touchCountDown = 0;
  screenTimeout = millis() + SCREEN_TIMEOUT;
}

// Called on setup or if Wifi connection has been lost
void setupWifi()
{
  clearDisplay();
  updateDisplayLine(HOSTNAME_LINE, hostname);
  updateDisplayLine(SSID_LINE, "Connecting to Wifi");
  size_t wifiCredentialCount = sizeof(wifiCredentials) / sizeof(WiFiCredentials);
  for (size_t index = 0; index < wifiCredentialCount; index += 1)
  {
    char buffer[100];
    sprintf(buffer, "%s@%s", wifiCredentials[index].ssid, wifiCredentials[index].password);
    println(buffer);
  }
  // We start by connecting to a WiFi network
  WiFi.disconnect(); // just in case
  WiFi.setHostname(hostname);
  boolean wifiConnected = false;
  // Nothing works without Wifi - loop until we are connected
  int wifiFound = -1; // index of the first known network found
  ssid = "Scanning";
  updateDisplayLine(SSID_LINE + 1, "Scanning for networks");
  println("Scanning for networks");
  // How many networks are visible?
  int visibleNetworkCount = WiFi.scanNetworks();
  print("Network count: ");
  println(String(visibleNetworkCount, 10).c_str());
  for (int index = 0; index < visibleNetworkCount; index += 1)
  {
    println(WiFi.SSID(index).c_str());
  }
  // Check all found networks
  for (int index1 = 0; index1 < visibleNetworkCount; index1 += 1)
  {
    print("Checking #");
    print(String(index1).c_str());
    print(" ");
    println(WiFi.SSID(index1).c_str());
    // try all known networks
    for (int index2 = 0; index2 < wifiCredentialCount; index2 += 1)
    {
      // Does the visible network have the SSID of this known network?
      if (strcmp(wifiCredentials[index2].ssid, WiFi.SSID(index1).c_str()) != 0)
      {
        // not equal
        print("Not matching ");
        println(wifiCredentials[index2].ssid);
      }
      else
      {
        // it does - stop going through our known networks
        wifiFound = index2;
        break;
      }
    }
    if (wifiFound >= 0)
    {
      // stop going through visible networks
      break;
    }
  }
  if (wifiFound >= 0)
  {
    // We are going to try and connect to this network
    ssid = wifiCredentials[wifiFound].ssid;
    updateDisplayLine(SSID_LINE + 1, "Trying");
    updateDisplayLine(SSID_LINE + 2, ssid);
    WiFi.begin(ssid, wifiCredentials[wifiFound].password);
    int attempts = 10; // We attempt only a limited number of times
    // we have a WiFi we can try and connect to
    while (WiFi.status() != WL_CONNECTED && attempts > 0)
    {
      attempts -= 1;
      delay(500);
    }
    wifiConnected = WiFi.status() == WL_CONNECTED;
    // If the Wifi is not connected we start the search all over again
  }

  // Finally, the Wifi is connected
  println("WiFi connected");
  println("IP address: ");
  println(WiFi.localIP().toString().c_str());
  println(WiFi.getHostname());
  // Update the display with SSID and IP address
  displayStatus();
}

// publish an (long) integer to the MQTT broker
void publishInteger(const char *topic, long value, bool retain)
{
  if (mqttClient.connected())
  {
    char message[20];
    sprintf(message, "%d", value);
    boolean result;
    result = mqttClient.publish(topic, (const uint8_t *)message, strlen(message), retain);
    /*
    print("Publish ");
    print(topic);
    print("=");
    print(message);
    print(" : ");
    println(result == true ? "OK" : "FAIL");
    */
  }
  else
  {
    displayStatus();
  }
}

// publish an (long) integer to the MQTT broker - default is to retain the value
void publishInteger(const char *topic, long value)
{
  publishInteger(topic, value, true);
}

// publish a string to the MQTT broker
void publishString(const char *topic, char *value, bool retain)
{
  if (mqttClient.connected())
  {
    boolean result;
    result = mqttClient.publish(topic, (const uint8_t *)value, strlen(value), retain);
    /*
    print("Publish ");
    print(topic);
    print("=");
    print(value);
    print(" : ");
    println(result == true ? "OK" : "FAIL");
    */
  }
  else
  {
    displayStatus();
  }
}

// publish a string to the MQTT broker - default is to retain the value
void publishString(const char *topic, char *value)
{
  publishString(topic, value, true);
}

// called when an MQTT topic we subscribe to gets an update
void mqttCallback(char *topic, byte *message, unsigned int length)
{
  // Convert the byte* message to a String
  String messageString;
  for (int index = 0; index < length; index += 1)
  {
    messageString += (char)message[index];
  }
  print("Received ");
  print(topic);
  print("=");
  println(messageString.c_str());
  if (strcmp(topic, MQTT_TOPIC_TIME) == 0)
  {
    strncpy(lastTimeReceived, messageString.c_str(), strlen(messageString.c_str()));
    updateDisplayLine(CLOCK_LINE, lastTimeReceived);
    print("Time is ");
    println(lastTimeReceived);
  }
}

// Setup the MQTT connection to the broker
void setupMQTT()
{
  print("Connecting MQTT to");
  println(ssid);
  // Try all known MQTT brokers in turn
  size_t mqttBrokerCount = sizeof(mqttBrokers) / sizeof(MqttBroker);
  for (int index = 0; index < mqttBrokerCount; index += 1)
  {
    // if the client is already connected - do nothing
    if (!mqttClient.connected())
    {
      updateDisplayLine(BROKER_TEXT_LINE, "MQTT connecting");
      updateDisplayLine(BROKER_IP_LINE, "");
      updateDisplayLine(BROKER_STATUS_LINE, "");
      if (strcmp(mqttBrokers[index].ssid, ssid) == 0)
      {
        // broker is on correct network
        mqttBroker = mqttBrokers[index].host;
        mqttPort = mqttBrokers[index].port;
        const char *mqttUsername = mqttBrokers[index].username;
        const char *mqttPassword = mqttBrokers[index].password;
        mqttClient.setServer(mqttBroker, mqttPort);
        mqttClient.setCallback(mqttCallback);
        // make up a unique client id
        String clientId = String(hostname) + "-" + String(WiFi.macAddress());
        print("Client ");
        println(clientId.c_str());
        print("Connecting to ");
        println(mqttBroker);
        char buffer[64];
        sprintf(buffer, "<%s> <%s>", mqttUsername, mqttPassword);
        println(buffer);
        updateDisplayLine(BROKER_IP_LINE, (String(mqttBroker) + ":" + String(mqttPort)).c_str());
        delay(1000);
        // Now try to connect to the MQTT broker
        if (mqttClient.connect(clientId.c_str(), mqttUsername, mqttPassword))
        {
          println("MQTT broker connected");
          updateDisplayLine(BROKER_STATUS_LINE, "MQTT connected");
          // Make sure we publish stuff so they are available in Node Red right away
          publishString(MQTT_TOPIC_INFO, (char *)"Ready");
          mqttClient.setCallback(mqttCallback);
          mqttClient.subscribe(MQTT_TOPIC_TIME);
          print("Subscribed to ");
          println(MQTT_TOPIC_TIME);
        }
        else
        {
          char buffer[32];
          int state = mqttClient.state();
          switch (state)
          {
          case -2:
            sprintf(buffer, "Failed - Not found");
            break;
          case 5:
            sprintf(buffer, "Failed - Not authorised");
            break;
          default:
            sprintf(buffer, "Failed %i", mqttClient.state());
            break;
          }
          println(buffer);
          updateDisplayLine(BROKER_STATUS_LINE, buffer);
        }
        delay(2000);
      }
    }
  }
}

void updateIntercom(int state)
{
  print("Intercom state");
  Serial.println(state);
  if (state == RINGING)
  {
    char *status = (char *)INTERCOM_RINGING;
    strncpy(intercomState, status, strlen(status));
    intercomState[strlen(status)] = '\0';
    if (screenTimeout == 0)
    {
      displayOn();
    }
    updateDisplayLine(DING_DONG_LINE, intercomState);
    publishInteger(MQTT_TOPIC_ALERT, 1);
    print("Ringing ");
    println(intercomState);
    screenTimeout = 0; // Keep screen on while ringing
  }
  else
  {
    char *status = (char *)INTERCOM_IDLE;
    strncpy(intercomState, status, strlen(status));
    intercomState[strlen(status)] = '\0';
    updateDisplayLine(DING_DONG_LINE, intercomState);
    publishInteger(MQTT_TOPIC_ALERT, 0);
    print("Idle ");
    println(intercomState);
    screenTimeout = millis() + SCREEN_TIMEOUT; // start screen timeout when ringing stops
  }
}

void setup()
{
  Serial.begin(115200);
  delay(100);

  setupPreferences();
  mySPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(mySPI);
  touchscreen.setRotation(orientation);

  // Start the tft display and set it to black
  tft.init();
  tft.setRotation(orientation); // This is the display in landscape (1 or 3) or portrait (0 or 2)
  tft.setFreeFont(FREE_FONT);

  Serial.println("Starting up");
  Serial.print("Display: ");
  Serial.print(String(tft.width()));
  Serial.print("x");
  Serial.println(String(tft.height()));
  displayLogo();
  delay(3000);
  pinMode(BACKLIGHT_PIN, OUTPUT);
  pinMode(INTERCOM_PIN, INPUT);

  setupWifi();
  setupMQTT();
  // Now we set the double reset flag to true
  setupRouting();
  server.begin();
  const char *ssid = WiFi.SSID().c_str();
  Serial.printf("SSID %s\n", ssid);
  if (!calibrated)
  {
    calibrating = TOP_LEFT;
    displayStatus();
  }
  displayOn();
  displayStatus();
  Serial.println("Ready.");
}

void loop()
{
  unsigned long now = millis();
  currentIntercomState = digitalRead(INTERCOM_PIN);
  if (lastIntercomState != currentIntercomState)
  {
    updateIntercom(currentIntercomState);
  }
  lastIntercomState = currentIntercomState;
  if (nextTouchTimer > 0 && now > nextTouchTimer)
  {
    handleTouchTimer();
  }
  mqttClient.loop();
  server.handleClient();
  if (touchscreen.tirqTouched() && touchscreen.touched())
  {
    TS_Point reading = touchscreen.getPoint();
    int16_t xChange = reading.x - touchPoint.x;
    int16_t yChange = reading.y - touchPoint.y;
    int16_t zChange = reading.z - touchPoint.z;
    if (abs(xChange) > 5 || abs(yChange) > 5)
    {
      touchPoint = reading;
      handleTouchStartEvent();
    }
  }
  else if (touchPoint.z != 0)
  {
    // touch end event with last value
    handleTouchEndEvent();
    // then clear the value
    touchPoint = TS_Point(0, 0, 0);
  }
  if (now > screenTimeout && currentIntercomState == IDLE)
  {
    displayOff();
  }
  delay(50);
}