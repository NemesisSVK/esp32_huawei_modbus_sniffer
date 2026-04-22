#pragma once
#include <Arduino.h>
#include <vector>

struct OtaStatusSnapshot {
    bool   configValid   = false;
    String configError;
    int    port          = 3232;
    int    windowSeconds = 600;
    bool   armed         = false;
    unsigned long remainingSeconds = 0;
    bool   inProgress    = false;
    int    progressPercent = 0;
    String command;    // "firmware" | "filesystem" | ""
    String lastError;
};

class OTAManager {
public:
    OTAManager();

    void setHostname(const String& hostname);

    /** Loads /ota.json (without arming). Useful for UI status after reboot. */
    void refreshConfigStatus();

    /** Arms OTA window (loads ota.json on demand). Returns false on config/FS errors. */
    bool armFromWeb(String& errorMessage);

    /** Must be called frequently from main loop */
    void loop();

    OtaStatusSnapshot snapshot() const;
    String getLogText() const;

private:
    struct OtaConfig {
        String password;
        int    port          = 3232;
        int    windowSeconds = 600;
    };

    bool loadConfig(String& errorMessage);
    void ensureArduinoOtaStarted();
    void addLogLine(const String& line);

    String hostname;
    OtaConfig cfg;
    bool   configValid;
    String configError;

    bool   armed;
    unsigned long armedUntilMs;

    bool   started;
    bool   inProgress;
    int    progressPercent;
    String command;
    int    lastCommand;
    String lastError;

    int    lastLoggedProgressDecile;
    std::vector<String> logLines;
    static const size_t MAX_LOG_LINES = 40;
};
