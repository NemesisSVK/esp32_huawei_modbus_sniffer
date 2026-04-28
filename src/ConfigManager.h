#pragma once
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include <Arduino.h>
#include "IPWhitelistManager.h"
#include "reg_groups.h"   // PublishTier, RegGroup, GRP_COUNT, TIER_COUNT, GROUP_INFO

// ============================================================
// Settings struct — mirrors data/config.json exactly
// ============================================================
struct Settings {
    struct Wifi {
        String ssid;
        String password;
    } wifi;

    struct Mqtt {
        String server;
        int    port       = 1883;
        String user;
        String password;
        String client_id;
        String base_topic;
    } mqtt;

    struct DeviceInfo {
        String name;
        String manufacturer;
        String model;
    } device_info;

    struct Network {
        String hostname;
        bool   mdns_enabled = true;
    } network;

    struct Security {
        bool   auth_enabled        = false;
        String username;
        String password;
        bool   ip_whitelist_enabled = false;
        std::vector<String> ip_ranges;
    } security;

    struct RS485 {
        int baud_rate        = 9600;
        int meter_slave_addr = 1;
    } rs485;

    struct Pins {
        int rs485_rx    = 16;
        int rs485_tx    = 17;
        int rs485_de_re = -1;
    } pins;

    struct Debug {
        bool logging_enabled        = true;  // mirrors debug.logging_enabled in config.json
        bool sensor_refresh_metrics = false; // show min/avg/max update intervals on home page
        bool raw_frame_dump         = false; // capture raw traffic selected by debug.raw_capture_profile (serial and/or raw stream)
        String raw_capture_profile  = "unknown_h41"; // unknown_h41 | all_frames
    } debug;

    struct RawStream {
        bool   enabled            = false;
        String host               = "";
        int    port               = 9900;
        int    queue_kb           = 256;
        int    reconnect_ms       = 1000;
        int    connect_timeout_ms = 1500;
        bool   serial_mirror      = false;
    } raw_stream;

    // Publish tier configuration — drives mqtt_tick() interval scheduling.
    // tier_interval_s[TIER_HIGH/MEDIUM/LOW] — flush interval per tier (seconds)
    // group_tier[RegGroup]                  — which tier each group belongs to
    // group_enabled[RegGroup]               — false = completely silent (no data, no availability)
    // All fields are configurable in config.json under "publish".
    struct Publish {
        int     tier_interval_s[TIER_COUNT] = { 10, 30, 60 };  // HIGH, MEDIUM, LOW defaults
        uint8_t group_tier[GRP_COUNT];    // PublishTier per group; initialised by setDefaults()
        bool    group_enabled[GRP_COUNT]; // false = group is completely silent (no data, no availability)
        struct ManualGroup {
            bool enabled = false;
            uint8_t tier = TIER_HIGH;
            // Selector format:
            // - "<register_name>"             -> match all sources for that register
            // - "<source_token>:<register>"   -> match specific source only
            //   source_token: fc03 | fc04 | h41_33 | h41_x
            std::vector<String> registers;
        } manual_group;
    } publish;
};

// ============================================================
// ConfigManager — load / save config.json from LittleFS
// ============================================================
class ConfigManager {
public:
    ConfigManager();

    /** Mount LittleFS and load config.json. Call once in setup(). */
    bool loadConfiguration();

    /** Write current settings back to config.json. */
    bool saveConfiguration();

    /** True after a successful loadConfiguration(). */
    bool isConfigLoaded() const { return configLoaded; }

    Settings&       getSettings()       { return settings; }
    const Settings& getSettings() const { return settings; }
    const String&   getLastError() const { return lastError; }

    /** Return current settings as a JSON string (for web API). */
    String getSettingsJson() const;
    /** Return current settings as pretty JSON (beautifier-style). */
    String getSettingsJsonPretty(bool redactPasswords = false) const;

    /**
     * Update settings from a JSON body (from web UI POST).
     * Saves automatically if update succeeds.
     */
    bool updateSettingsFromJson(const String& jsonBody);

private:
    Settings settings;
    bool     configLoaded = false;
    String   lastError;

    void setDefaults();
    bool parseJson(const JsonDocument& doc);
    void buildJson(JsonDocument& doc) const;
};
