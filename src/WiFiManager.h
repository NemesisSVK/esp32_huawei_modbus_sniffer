#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <WiFi.h>
#include <Arduino.h>

/**
 * WiFiManager — non-blocking WiFi connection and watchdog.
 * Call setupWiFi() once in setup(), then checkWiFiConnection() in loop().
 */
class WiFiManager {
public:
    WiFiManager();

    void setupWiFi(const String& ssid, const String& password,
                   const String& hostname);
    void checkWiFiConnection();
    bool isConnected() const;
    int  getRetryCount() const { return wifi_retry_count; }

private:
    String wifi_ssid;
    String wifi_password;

    bool connecting = false;
    unsigned long connectAttemptStart = 0;
    static const unsigned long CONNECT_ATTEMPT_TIMEOUT_MS = 10000;
    static const unsigned long WIFI_CHECK_INTERVAL_MS     = 30000;

    unsigned long last_wifi_check = 0;
    int wifi_retry_count = 0;
};

#endif // WIFIMANAGER_H
