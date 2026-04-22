#include "OTAManager.h"
#include "UnifiedLogger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "esp_task_wdt.h"   // for esp_task_wdt_reset() in onProgress

OTAManager::OTAManager()
    : configValid(false),
      armed(false),
      armedUntilMs(0),
      started(false),
      inProgress(false),
      progressPercent(0),
      lastCommand(-1),
      lastLoggedProgressDecile(-1) {}

void OTAManager::setHostname(const String& newHostname) {
    hostname = newHostname;
}

void OTAManager::refreshConfigStatus() {
    String err;
    (void)loadConfig(err);
}

bool OTAManager::loadConfig(String& errorMessage) {
    configValid = false;
    configError = "";

    if (!LittleFS.begin(false)) {
        errorMessage = "LittleFS not mounted";
        configError  = errorMessage;
        return false;
    }
    if (!LittleFS.exists("/ota.json")) {
        errorMessage = "/ota.json not found — create it to enable OTA";
        configError  = errorMessage;
        return false;
    }

    File f = LittleFS.open("/ota.json", "r");
    if (!f) {
        errorMessage = "Failed to open /ota.json";
        configError  = errorMessage;
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        errorMessage = String("Invalid /ota.json: ") + err.c_str();
        configError  = errorMessage;
        return false;
    }

    JsonObjectConst otaObj = doc["ota"];
    if (otaObj.isNull()) {
        errorMessage = "/ota.json missing 'ota' object";
        configError  = errorMessage;
        return false;
    }

    String password     = otaObj["password"] | "";
    int    port         = otaObj["port"]           | 3232;
    int    windowSeconds = otaObj["window_seconds"] | 600;

    password.trim();
    if (password.length() == 0) { errorMessage = "ota.password must be non-empty"; configError = errorMessage; return false; }
    if (port < 1 || port > 65535) { errorMessage = "ota.port must be 1..65535"; configError = errorMessage; return false; }
    if (windowSeconds < 10 || windowSeconds > 3600) { errorMessage = "ota.window_seconds must be 10..3600"; configError = errorMessage; return false; }

    cfg.password      = password;
    cfg.port          = port;
    cfg.windowSeconds = windowSeconds;
    configValid = true;
    configError = "";
    return true;
}

void OTAManager::ensureArduinoOtaStarted() {
    if (started) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (!configValid) return;

    if (hostname.length() > 0) ArduinoOTA.setHostname(hostname.c_str());
    ArduinoOTA.setPort(static_cast<uint16_t>(cfg.port));
    ArduinoOTA.setPassword(cfg.password.c_str());
    ArduinoOTA.setRebootOnSuccess(false);   // let firmware + fs flash sequentially

    ArduinoOTA.onStart([this]() {
        inProgress = true;
        lastError  = "";
        progressPercent = 0;
        lastLoggedProgressDecile = -1;
        lastCommand = ArduinoOTA.getCommand();
        command = (lastCommand == U_FLASH) ? "firmware" :
                  (lastCommand == U_SPIFFS) ? "filesystem" : "";
        addLogLine(String("[OTA] Start (") + (command.length() ? command : "unknown") + ")");
    });

    ArduinoOTA.onEnd([this]() {
        inProgress      = false;
        progressPercent = 100;
        addLogLine("[OTA] End");
        // Reboot only after filesystem upload so PlatformIO can do upload+uploadfs in one go
        if (lastCommand == U_SPIFFS) {
            addLogLine("[OTA] Rebooting after filesystem update");
            delay(150);
            ESP.restart();
        }
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        if (total == 0) return;
        // Feed the hardware watchdog on every chunk.  ArduinoOTA.handle() drives
        // the entire transfer synchronously inside the loop task; without this,
        // a 3.5 MB LittleFS image (~40 s) trips the 30 s TWDT and hard-reboots
        // the device mid-transfer.
        esp_task_wdt_reset();
        const int pct    = static_cast<int>((progress * 100UL) / total);
        progressPercent  = pct;
        const int decile = pct / 10;
        if (decile != lastLoggedProgressDecile) {
            lastLoggedProgressDecile = decile;
            addLogLine(String("[OTA] Progress ") + String(decile * 10) + "%");
        }
    });

    ArduinoOTA.onError([this](ota_error_t error) {
        inProgress = false;
        String msg = "[OTA] Error " + String(static_cast<int>(error));
        switch (error) {
            case OTA_AUTH_ERROR:    msg += " (auth)";    break;
            case OTA_BEGIN_ERROR:   msg += " (begin)";   break;
            case OTA_CONNECT_ERROR: msg += " (connect)"; break;
            case OTA_RECEIVE_ERROR: msg += " (receive)"; break;
            case OTA_END_ERROR:     msg += " (end)";     break;
            default: break;
        }
        lastError = msg;
        addLogLine(msg);
    });

    ArduinoOTA.begin();
    started = true;
    addLogLine(String("[OTA] Ready on port ") + String(cfg.port));
}

bool OTAManager::armFromWeb(String& errorMessage) {
    errorMessage = "";
    String cfgErr;
    if (!loadConfig(cfgErr)) { errorMessage = cfgErr; return false; }

    armedUntilMs = millis() + (static_cast<unsigned long>(cfg.windowSeconds) * 1000UL);
    armed        = true;
    addLogLine(String("[OTA] Armed for ") + String(cfg.windowSeconds) + "s");
    ensureArduinoOtaStarted();
    return true;
}

void OTAManager::loop() {
    if (!armed) return;
    const unsigned long now = millis();
    if (!inProgress && armedUntilMs != 0 && static_cast<long>(armedUntilMs - now) <= 0) {
        armed = false;
        addLogLine("[OTA] Window expired");
        return;
    }
    ensureArduinoOtaStarted();
    if (!started) return;
    ArduinoOTA.handle();
}

OtaStatusSnapshot OTAManager::snapshot() const {
    OtaStatusSnapshot s;
    s.configValid     = configValid;
    s.configError     = configError;
    s.port            = cfg.port;
    s.windowSeconds   = cfg.windowSeconds;
    s.armed           = armed;
    s.inProgress      = inProgress;
    s.progressPercent = progressPercent;
    s.command         = command;
    s.lastError       = lastError;
    if (armed) {
        const unsigned long now = millis();
        s.remainingSeconds = (armedUntilMs > now) ? (armedUntilMs - now) / 1000UL : 0;
    }
    return s;
}

String OTAManager::getLogText() const {
    String out;
    for (const auto& line : logLines) { out += line; out += "\n"; }
    return out;
}

void OTAManager::addLogLine(const String& line) {
    if (logLines.size() >= MAX_LOG_LINES) logLines.erase(logLines.begin());
    logLines.push_back(line);
    UnifiedLogger::info("%s\n", line.c_str());
}
