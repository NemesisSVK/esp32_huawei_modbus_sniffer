#include "WiFiManager.h"
#include "UnifiedLogger.h"

WiFiManager::WiFiManager() {}

void WiFiManager::setupWiFi(const String& ssid, const String& password,
                             const String& hostname) {
    wifi_ssid     = ssid;
    wifi_password = password;

    WiFi.setHostname(hostname.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    delay(100);
    UnifiedLogger::info("[WiFi] connecting to '%s'\n", ssid.c_str());
}

void WiFiManager::checkWiFiConnection() {
    static bool wasConnected = true;

    if (WiFi.status() != WL_CONNECTED) {
        wasConnected = false;
        unsigned long now = millis();

        // If a reconnect attempt is in progress, check for timeout
        if (connecting) {
            if (now - connectAttemptStart >= CONNECT_ATTEMPT_TIMEOUT_MS) {
                UnifiedLogger::warning("[WiFi] reconnect attempt timed out (status=%d)\n",
                                      (int)WiFi.status());
                connecting = false;
            }
            return;
        }

        // Throttle: only try every 30s
        if (now - last_wifi_check >= WIFI_CHECK_INTERVAL_MS || last_wifi_check == 0) {
            last_wifi_check = now;
            wifi_retry_count++;
            UnifiedLogger::warning("[WiFi] disconnected — reconnecting (attempt %d)...\n",
                                  wifi_retry_count);
            if (WiFi.status() != WL_IDLE_STATUS) WiFi.disconnect(true);
            WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
            connecting         = true;
            connectAttemptStart = now;
        }
    } else {
        if (!wasConnected) {
            UnifiedLogger::info("[WiFi] connected — IP=%s\n",
                               WiFi.localIP().toString().c_str());
            wifi_retry_count = 0;
            connecting        = false;
            wasConnected      = true;
        }
    }
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}
