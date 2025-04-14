#ifndef CREDENTIALS_H
#define CREDENTIALS_H

// Copy this file to credentials.h and fill in the real credentials

struct MqttBroker
{
    char ssid[32];
    char host[32];
    int port;
    char username[32];
    char password[32];
};

struct WiFiCredentials
{
    char ssid[32]; // 32 seems long enough for SSID or password
    char password[32];
};

const WiFiCredentials wifiCredentials[] = {
    {"SSID1", "password1"},
    {"SSID2", "password2"}};

const MqttBroker mqttBrokers[] = {
    {"SSID1", "192.168.0.1", 1883, "user1", "pass1"},
    {"SSID1", "192.168.0.2", 1883, "user2", "pass2"},
    {"SSID2", "192.168.1.1", 1883, "user3", "pass3"}};

#endif