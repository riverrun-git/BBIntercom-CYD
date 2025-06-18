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

SPIClass mySPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS /*, XPT2046_IRQ*/);

TFT_eSPI tft = TFT_eSPI();

char hostname[32] = "BB Intercom";
const char *ssid = "Searching"; // displayed as a placeholder while not connected
char lastTimeReceived[32] = "--:--";
char uptime[32] = "";

#define INTERCOM_IDLE ""
#define INTERCOM_RINGING "DING DONG"
char intercomState[32] = INTERCOM_IDLE;

#define UNCALIBRATED TS_Point(-1, -1, 0);
TS_Point touchPoint;
TS_Point calibrationTopLeft = UNCALIBRATED;
TS_Point calibrationTopRight = UNCALIBRATED;
TS_Point calibrationBottomLeft = UNCALIBRATED;
TS_Point calibrationBottomRight = UNCALIBRATED;

#define DISPLAY_OFF 0
#define DISPLAY_LOGO 1
#define DISPLAY_STATUS 3
#define DISPLAY_RINGING 4

#define TOUCH_ACTION_NONE 0
#define TOUCH_ACTION_RESET 1
#define TOUCH_ACTION_RING 2
#define TOUCH_ACTION_RECONNECT 3

#define SCREEN_TIMEOUT 1000 * 60 * 5; // 5 minutes sounds reasonable

// #define FONT_HEIGHT 30
#define FONT_NUMBER 4 // Use GFXFF to use an Adafruit free font - I like font 4
#define FREE_FONT FF6 // Only has an effect if FONT_NUMBER is set to GFXFF
#define FONT_HEIGHT 26
#define DISPLAY_LINES 8

char lines[DISPLAY_LINES][32];
char displayed[DISPLAY_LINES][32];

uint8_t displayMode = DISPLAY_OFF;
uint8_t touchAction = TOUCH_ACTION_NONE;

void clearDisplay()
{
  tft.fillScreen(BACKGROUND_COLOUR);
  tft.setTextColor(TEXT_COLOUR, BACKGROUND_COLOUR);
  for (uint8_t index = 0; index < DISPLAY_LINES; index += 1)
  {
    displayed[index][0] = '\0';
  }
}

void setDisplayMode(uint8_t mode)
{
  if (mode != displayMode)
  {
    displayMode = mode;
    print("Display mode set to ");
    println(String(displayMode).c_str());
    clearDisplay();
  }
}

unsigned long screenTimeout = 0;  // 0 or timestamp when last released
unsigned long touchStartTime = 0; // 0 or timestamp when pressed
unsigned long nextTouchTimer = 0; // 0 or or timestamp
unsigned long uptimeUpdateMillis = 0;
const long uptimeUpdateInterval = 60000; // every minute

#define IDLE 1
#define RINGING 0
int lastIntercomState = IDLE;
int currentIntercomState = IDLE;

Preferences preferences;
WebServer server(80);

int orientation = 3;

int launchCount = 0;
bool calibrated = false;
int calibrating = 0; // The point currently being calibrated or 0

#define SSID_LINE 1
#define IP_LINE 2
#define BROKER_TEXT_LINE 3
#define BROKER_IP_LINE 4
#define BROKER_STATUS_LINE 5
#define TOUCH_ACTION_LINE 6
#define DING_DONG_LINE 6
#define UPTIME_LINE 7
#define CLOCK_LINE 8

WiFiClient wifiClient;               // The Wifi connection
PubSubClient mqttClient(wifiClient); // The MQTT connection
const char *mqttBroker = "";
int mqttPort = 0;
const char *mqttUsername = "";
const char *mqttPassword = "";

const char *MQTT_TOPIC_ALERT = "/intercom/active";
const char *MQTT_TOPIC_INFO = "/intercom/info";
const char *MQTT_TOPIC_TIME = "/intercom/time"; // incoming
const char *MQTT_TOPIC_UPTIME = "/intercom/uptime";

char uptimeText[32];
void updateUptimeText(unsigned long milliseconds)
{
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  unsigned long minutes_final = minutes % 60;
  unsigned long hours_final = hours % 24;

  sprintf(uptimeText, "%lud %02lu:%02lu", days, hours_final, minutes_final);
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

void displayRinging()
{
  clearDisplay();
  uint8_t offset = tft.height() / 10;
  tft.setTextFont(GFXFF);
  tft.setTextColor(TFT_YELLOW);
  tft.setFreeFont(FSSB24);
  tft.setTextSize(2);
  tft.drawCentreString("DING", tft.width() / 2, offset, GFXFF);
  tft.drawCentreString("DONG", tft.width() / 2, tft.height() / 2, GFXFF);
  tft.setTextSize(1);
  tft.setFreeFont(NULL);
  tft.setTextFont(FONT_NUMBER);
  tft.setTextColor(TEXT_COLOUR);
}

void displayLogo()
{
  clearDisplay();
  tft.pushImage((tft.width() - LOGO_WIDTH) / 2, 0, LOGO_WIDTH, LOGO_HEIGHT, logo);
}

void setLineText(int line, const char *text)
{
  print("Set line #");
  print(String(line).c_str());
  print(" to ");
  println(text);
  // Lines start at 1, the array at 0
  strncpy(lines[line - 1], text, strlen(text));
  lines[line - 1][strlen(text)] = '\0';
  /*
  // print lines to serial on every update - for debugging - comment out most of the time
  for (uint8_t index = 0; index < DISPLAY_LINES; index += 1)
  {
    print(String(index + 1).c_str());
    print(": ");
    println(lines[index]);
  }
  */
}

void drawDisplayLine(int line, const char *text)
{
  int displayHeight = tft.height();
  int lineHeight = displayHeight / DISPLAY_LINES;
  int gap = lineHeight - FONT_HEIGHT;
  int topGap = gap / 2;
  int yTop = line * lineHeight;
  int yText = yTop + topGap;
  tft.fillRect(0, yTop, tft.width(), lineHeight, BACKGROUND_COLOUR);
  tft.drawCentreString(text, tft.width() / 2, yText, FONT_NUMBER);
}

// display the connection info (and cross hair if calibrating)
void updateDisplay()
{
  print("Update Display status: ");
  switch (displayMode)
  {
  case DISPLAY_LOGO:
    println("Logo");
    displayLogo();
    break;
  case DISPLAY_STATUS:
    println("Status");
    for (uint8_t index = 0; index < DISPLAY_LINES; index += 1)
    {
      if (strcmp(lines[index], displayed[index]) != 0 || calibrating)
      {
        drawDisplayLine(index, lines[index]);
        strncpy(displayed[index], lines[index], strlen(lines[index]));
        displayed[index][strlen(lines[index])] = '\0';
      }
    }
    if (calibrating)
    {
      drawCrosshair(calibrating);
    }
    break;
  case DISPLAY_RINGING:
    println("Ringing");
    displayRinging();
    break;
  default:
    println("Display Off");
    // do nothing
    break;
  }
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
  setDisplayMode(DISPLAY_OFF);
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
    lastTimeReceived[strlen(messageString.c_str())] = '\0';
    setLineText(CLOCK_LINE, lastTimeReceived);
    updateDisplay();
    print("Time is ");
    println(lastTimeReceived);
  }
}

void connectBroker()
{
  String clientId = String(hostname) + "-" + String(WiFi.macAddress());
  if (mqttClient.connect(clientId.c_str(), mqttUsername, mqttPassword))
  {
    println("MQTT broker connected");
    setLineText(BROKER_TEXT_LINE, "MQTT broker:");
    setLineText(BROKER_STATUS_LINE, "MQTT connected");
    mqttClient.setCallback(mqttCallback);
    mqttClient.subscribe(MQTT_TOPIC_TIME);
    print("Subscribed to ");
    println(MQTT_TOPIC_TIME);
    updateDisplay();
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
    setLineText(BROKER_STATUS_LINE, buffer);
    updateDisplay();
  }
}
// publish an (long) integer to the MQTT broker
void publishInteger(const char *topic, long value, bool retain)
{
  if (!mqttClient.connected())
  {
    connectBroker();
  }
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
}

// publish an (long) integer to the MQTT broker - default is to retain the value
void publishInteger(const char *topic, long value)
{
  publishInteger(topic, value, true);
}

// publish a string to the MQTT broker
void publishString(const char *topic, char *value, bool retain)
{
  if (!mqttClient.connected())
  {
    connectBroker();
  }
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
}

// publish a string to the MQTT broker - default is to retain the value
void publishString(const char *topic, char *value)
{
  publishString(topic, value, true);
}

// Called on setup or if Wifi connection has been lost
void setupWifi()
{
  setLineText(SSID_LINE, "Connecting to Wifi");
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
  setLineText(SSID_LINE + 1, "Scanning for networks");
  updateDisplay();
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
    setLineText(SSID_LINE + 1, "Trying");
    setLineText(SSID_LINE + 2, ssid);
    updateDisplay();
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
  if (WiFi.status() == WL_CONNECTED)
  {
    setLineText(SSID_LINE, WiFi.SSID().c_str());
    setLineText(IP_LINE, WiFi.localIP().toString().c_str());
  }
  else
  {
    setLineText(SSID_LINE, "WiFi not connected");
    setLineText(IP_LINE, "Restart to connect");
  }
  updateDisplay();
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
      setLineText(BROKER_TEXT_LINE, "MQTT connecting");
      setLineText(BROKER_IP_LINE, "");
      setLineText(BROKER_STATUS_LINE, "");
      updateDisplay();
      if (strcmp(mqttBrokers[index].ssid, ssid) == 0)
      {
        // broker is on correct network
        mqttBroker = mqttBrokers[index].host;
        mqttPort = mqttBrokers[index].port;
        mqttUsername = mqttBrokers[index].username;
        mqttPassword = mqttBrokers[index].password;
        mqttClient.setServer(mqttBroker, mqttPort);
        mqttClient.setCallback(mqttCallback);
        mqttClient.setKeepAlive(120);
        // make up a unique client id
        String clientId = String(hostname) + "-" + String(WiFi.macAddress());
        print("Client ");
        println(clientId.c_str());
        print("Connecting to ");
        println(mqttBroker);
        char buffer[64];
        sprintf(buffer, "<%s> <%s>", mqttUsername, mqttPassword);
        println(buffer);
        setLineText(BROKER_IP_LINE, (String(mqttBroker) + ":" + String(mqttPort)).c_str());
        updateDisplay();
        delay(1000);
        // Now try to connect to the MQTT broker
        connectBroker();
        if (mqttClient.connected())
        {
          // Make sure we publish stuff so they are available in Node Red right away
          publishString(MQTT_TOPIC_INFO, (char *)WiFi.localIP().toString().c_str());
        }
        delay(2000);
      }
    }
  }
}

void updateIntercom(int state)
{
  print("Intercom state ");
  Serial.println(state == RINGING ? "Ringing" : "Idle");
  if (state == RINGING)
  {
    char *status = (char *)INTERCOM_RINGING;
    strncpy(intercomState, status, strlen(status));
    intercomState[strlen(status)] = '\0';
    if (displayMode == DISPLAY_OFF)
    {
      displayOn();
    }
    setLineText(DING_DONG_LINE, intercomState);
    setDisplayMode(DISPLAY_RINGING);
    updateDisplay();
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
    setLineText(DING_DONG_LINE, intercomState);
    setDisplayMode(DISPLAY_STATUS);
    updateDisplay();
    publishInteger(MQTT_TOPIC_ALERT, 0);
    print("Idle ");
    println(intercomState);
    screenTimeout = millis() + SCREEN_TIMEOUT; // start screen timeout when ringing stops
  }
}

int touchCountDown = 0;

void handleTouchTimer()
{
  if (touchCountDown == 0)
  {
    touchCountDown = 5;
    println("In 5");
  }
  else
  {
    char buffer[16];
    touchCountDown -= 1;
    sprintf(buffer, "Minus one = %d", touchCountDown);
    println(buffer);
    if (touchCountDown == 0)
    {
      switch (touchAction)
      {
      case TOUCH_ACTION_RESET:
        resetStoredCalibration();
        ESP.restart();
        break;
      case TOUCH_ACTION_RING:
        updateIntercom(RINGING);
        break;
      }
    }
  }
  char message[32] = "Do nothing";
  switch (touchAction)
  {
  case TOUCH_ACTION_RESET:
    sprintf(message, "Reset in %ds", touchCountDown);
    break;
  case TOUCH_ACTION_RING:
    sprintf(message, "Ring in %ds", touchCountDown);
    break;
  }
  setLineText(TOUCH_ACTION_LINE, message);
  updateDisplay();
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
    clearDisplay();
    updateDisplay();
    break;
  }
  updateDisplay();
}

int mapTouchToScreenX(int x)
{
  int xTouchMin = (calibrationTopLeft.x + calibrationBottomLeft.x) / 2;
  int xTouchMax = (calibrationTopRight.x + calibrationBottomRight.x) / 2;
  int xScreenMin = CROSSHAIR_MARGIN + CROSSHAIR_SIZE / 2;
  int xScreenMax = tft.width() - CROSSHAIR_MARGIN - CROSSHAIR_SIZE / 2;
  int mapped = map(x, xTouchMin, xTouchMax, xScreenMin, xScreenMax);
  return mapped;
}

int mapTouchToScreenY(int y)
{
  int yTouchMin = (calibrationTopLeft.y + calibrationTopRight.y) / 2;
  int yTouchMax = (calibrationBottomLeft.y + calibrationBottomRight.y) / 2;
  int yScreenMin = CROSSHAIR_MARGIN + CROSSHAIR_SIZE / 2;
  int yScreenMax = tft.height() - CROSSHAIR_MARGIN - CROSSHAIR_SIZE / 2;
  int mapped = map(y, yTouchMin, yTouchMax, yScreenMin, yScreenMax);
  return mapped;
}

void handleTouchStartEvent()
{
  char buffer[64] = "";
  if (displayMode == DISPLAY_OFF)
  {
    // display is off
    displayOn();
    setDisplayMode(DISPLAY_LOGO);
    updateDisplay();
    sprintf(buffer, "WAKE x:%d y:%d z:%d", touchPoint.x, touchPoint.y, touchPoint.z);
    println(buffer);
  }
  else if (touchStartTime == 0 && displayMode != DISPLAY_LOGO)
  {
    sprintf(buffer, "TOUCH x:%d y:%d z:%d", touchPoint.x, touchPoint.y, touchPoint.z);
    println(buffer);
    if (calibrated)
    {
      int16_t screenX = mapTouchToScreenX(touchPoint.x);
      int16_t screenY = mapTouchToScreenY(touchPoint.y);
      int16_t lineHeight = tft.height() / DISPLAY_LINES;
      int16_t lineTouched = screenY / lineHeight + 1;
      sprintf(buffer, "SCREEN x:%d y:%d LINE %d", screenX, screenY, lineTouched);
      println(buffer);
      switch (lineTouched)
      {
      case UPTIME_LINE:
      case CLOCK_LINE:
        touchAction = TOUCH_ACTION_RESET;
        break;
      case TOUCH_ACTION_LINE:
        touchAction = TOUCH_ACTION_RING;
        break;
      default:
        touchAction = TOUCH_ACTION_NONE;
        break;
      }
    }
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
  if (strncmp(intercomState, INTERCOM_RINGING, strlen(INTERCOM_RINGING)) == 0)
  {
    println("Ringing turned off");
    updateIntercom(IDLE);
  }
  setDisplayMode(DISPLAY_STATUS);
  println("TOUCH END");
  setLineText(TOUCH_ACTION_LINE, "");
  updateDisplay();
  touchStartTime = 0;
  nextTouchTimer = 0;
  touchCountDown = 0;
  touchAction = TOUCH_ACTION_NONE;
  screenTimeout = millis() + SCREEN_TIMEOUT;
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

void handleIntercom()
{
  if (server.hasArg("intercom"))
  {
    println("DINGDONG");
    String body = server.arg("intercom");
    print("Arg ");
    println(body.c_str());
    if (strcmp(body.c_str(), "1") == 0)
    {
      updateIntercom(RINGING);
    }
    else
    {
      updateIntercom(IDLE);
    }
  }
  server.send(200, "text/plain", "Thank you.");
}

void setupRouting()
{
  server.on("/time", handleTime);
  server.on("/colour", HTTP_POST, handleColour);
  server.on("/intercom", HTTP_POST, handleIntercom);
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
  setDisplayMode(DISPLAY_LOGO);
  updateDisplay();
  delay(3000);
  setDisplayMode(DISPLAY_STATUS);
  pinMode(BACKLIGHT_PIN, OUTPUT);
  pinMode(INTERCOM_PIN, INPUT_PULLUP); // Ringing is 0, idle is 1, so we pull up as default

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
    updateDisplay();
  }
  displayOn();
  updateDisplay();
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
  if (now > uptimeUpdateMillis + uptimeUpdateInterval)
  {
    uptimeUpdateMillis += uptimeUpdateInterval;
    updateUptimeText(uptimeUpdateMillis);
    char uptimeDisplayText[32] = "";
    sprintf(uptimeDisplayText, "Uptime: %s", uptimeText);
    setLineText(UPTIME_LINE, uptimeDisplayText);
    publishString(MQTT_TOPIC_UPTIME, uptimeText);
    updateDisplay();
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
  if (now > screenTimeout && currentIntercomState == IDLE && displayMode == DISPLAY_STATUS)
  {
    displayOff();
  }
  delay(50);
}