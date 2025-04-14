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

#define BACKLIGHT_PIN 21
#define INTERCOM_PIN 22

// Touchscreen pins
#define XPT2046_IRQ 36  // T_IRQ
#define XPT2046_MOSI 32 // T_DIN
#define XPT2046_MISO 39 // T_OUT
#define XPT2046_CLK 25  // T_CLK
#define XPT2046_CS 33   // T_CS

SPIClass mySPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS /*, XPT2046_IRQ*/);

TFT_eSPI tft = TFT_eSPI();

char hostname[32] = "BB Intercom";
const char *ssid = "Searching"; // displayed as a placeholder while not connected
char lastTimeReceived[32] = "--:--";

#define INTERCOM_IDLE ""
#define INTERCOM_RINGING "DING DONG"
char intercomState[32] = INTERCOM_IDLE;

#define SCREEN_TIMEOUT 1000 * 30
#define UNTOUCHED 0
#define TOUCHED 1
int lastTouchState = UNTOUCHED;
int currentTouchState;

unsigned long screenTimeout = 0;  // 0 or timestamp when last released
unsigned long touchStartTime = 0; // 0 or timestamp when pressed
unsigned long nextTouchTimer = 0; // 0 or or timestamp

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
}

void displayOn()
{
  // digitalWrite(BACKLIGHT_PIN, LOW);
  displayStatus();
  screenTimeout = millis() + SCREEN_TIMEOUT;
}

void displayOff()
{
  // digitalWrite(BACKLIGHT_PIN, HIGH);
  clearDisplay();
  screenTimeout = 0;
}

// Load or initialize preferences
void setupPreferences()
{
  preferences.begin(PREFERENCES_NAMESPACE);
  /*
  if (preferences.isKey(PREFERENCES_KEY_LAUCH_COUNT))
  {
    launchCount = preferences.getInt(PREFERENCES_KEY_LAUCH_COUNT) + 1;
  }
  else
  {
    // first launch
    launchCount = 1;
  }
  preferences.putInt(PREFERENCES_KEY_LAUCH_COUNT, launchCount);
  Serial.print("Launch #");
  Serial.println(launchCount);
  if (preferences.isKey(PREFERENCES_KEY_DOUBLE_RESET))
  {
    isDoubleReset = preferences.getBool(PREFERENCES_KEY_DOUBLE_RESET);
  }
  else
  {
    preferences.putBool(PREFERENCES_KEY_DOUBLE_RESET, isDoubleReset); // i.e. false
  }
  */
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
      ESP.restart();
    }
  }
  char message[32];
  sprintf(message, "Reset in %is", touchCountDown);
  updateDisplayLine(RESET_LINE, message);
  nextTouchTimer = millis() + 1000;
}

void handleTouchStart()
{
  println("TOUCH");
  touchStartTime = millis();
  nextTouchTimer = millis() + 1000; // every second
  if (screenTimeout == 0)
  {
    // display is off
    displayOn();
  }
}

void handleTouchEnd()
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
    print("Time is");
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
  }
}

void setup()
{
  Serial.begin(115200);
  delay(100);
  Serial.println("Starting up");

  setupPreferences();
  mySPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySPI);
  ts.setRotation(orientation);

  // Start the tft display and set it to black
  tft.init();
  tft.setRotation(orientation); // This is the display in landscape (1 or 3) or portrait (0 or 2)
  tft.setFreeFont(FREE_FONT);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  pinMode(INTERCOM_PIN, INPUT);

  setupWifi();
  setupMQTT();
  // Now we set the double reset flag to true
  setupRouting();
  server.begin();
  const char *ssid = WiFi.SSID().c_str();
  Serial.printf("SSID %s\n", ssid);
  displayOn();
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
  currentTouchState = (ts.tirqTouched() && ts.touched());
  if (lastTouchState == TOUCHED && currentTouchState == UNTOUCHED)
  {
    handleTouchEnd();
  }
  else if (lastTouchState == UNTOUCHED && currentTouchState == TOUCHED)
  {
    handleTouchStart();
  }
  lastTouchState = currentTouchState;
  if (now > screenTimeout)
  {
    displayOff();
  }
  delay(50);
}