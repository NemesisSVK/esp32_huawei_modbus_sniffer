#include "ConfigManager.h"
#include "UnifiedLogger.h"
#include "PsramJsonAllocator.h"
#include <Arduino.h>

#define CONFIG_PATH "/config.json"

ConfigManager::ConfigManager() {
    setDefaults();
}

void ConfigManager::setDefaults() {
    settings.wifi.ssid                    = "";
    settings.wifi.password                = "";
    settings.mqtt.server                  = "192.168.1.100";
    settings.mqtt.port                    = 1883;
    settings.mqtt.user                    = "";
    settings.mqtt.password                = "";
    settings.mqtt.client_id               = "huawei-sniffer";
    settings.mqtt.base_topic              = "huawei_solar";
    settings.device_info.name             = "Huawei Solar Sniffer";
    settings.device_info.manufacturer     = "DIY";
    settings.device_info.model            = "ESP32-S3 R16N8";
    settings.network.hostname             = "huawei-sniffer";
    settings.network.mdns_enabled         = true;
    settings.security.auth_enabled        = false;
    settings.security.username            = "admin";
    settings.security.password            = "";
    settings.security.ip_whitelist_enabled = false;
    settings.security.ip_ranges.clear();
    settings.rs485.baud_rate              = 9600;
    settings.rs485.meter_slave_addr       = 1;
    settings.pins.rs485_rx                = 16;
    settings.pins.rs485_tx                = 17;
    settings.pins.rs485_de_re             = -1;
    settings.debug.logging_enabled        = true;
    settings.debug.sensor_refresh_metrics = false;
    settings.debug.raw_frame_dump         = false;
    settings.debug.raw_capture_profile    = "unknown_h41";
    settings.raw_stream.enabled           = false;
    settings.raw_stream.host              = "";
    settings.raw_stream.port              = 9900;
    settings.raw_stream.queue_kb          = 256;
    settings.raw_stream.reconnect_ms      = 1000;
    settings.raw_stream.connect_timeout_ms= 1500;
    settings.raw_stream.serial_mirror     = false;
    // publish tiers
    settings.publish.tier_interval_s[TIER_HIGH]   = 10;
    settings.publish.tier_interval_s[TIER_MEDIUM] = 30;
    settings.publish.tier_interval_s[TIER_LOW]    = 60;
    // group tier assignments and enabled flags — defaults from reg_groups.h
    for (int g = 0; g < GRP_COUNT; g++) {
        settings.publish.group_tier[g]    = (uint8_t)GROUP_INFO[g].default_tier;
        settings.publish.group_enabled[g] = true;
    }
}

bool ConfigManager::loadConfiguration() {
    // Mount LittleFS (format=false — we don't format on failure)
    if (!LittleFS.begin(false)) {
        UnifiedLogger::error("[CFG] LittleFS mount failed — using defaults\n");
        configLoaded = false;
        return false;
    }

    if (!LittleFS.exists(CONFIG_PATH)) {
        UnifiedLogger::warning("[CFG] config.json not found — using defaults\n");
        configLoaded = false;
        return false;
    }

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) {
        UnifiedLogger::error("[CFG] cannot open config.json — using defaults\n");
        configLoaded = false;
        return false;
    }

    PsramJsonAllocator alloc;
    JsonDocument doc(&alloc);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        UnifiedLogger::error("[CFG] config.json parse error: %s — using defaults\n", err.c_str());
        configLoaded = false;
        return false;
    }

    setDefaults();
    parseJson(doc);
    configLoaded = true;

    UnifiedLogger::info("[CFG] loaded — wifi='%s' mqtt=%s:%d host=%s\n",
                        settings.wifi.ssid.c_str(),
                        settings.mqtt.server.c_str(), settings.mqtt.port,
                        settings.network.hostname.c_str());
    return true;
}

bool ConfigManager::parseJson(const JsonDocument& doc) {
    // wifi
    if (doc["wifi"]["ssid"].is<const char*>())     settings.wifi.ssid     = doc["wifi"]["ssid"].as<String>();
    if (doc["wifi"]["password"].is<const char*>())  settings.wifi.password = doc["wifi"]["password"].as<String>();

    // mqtt
    if (doc["mqtt"]["server"].is<const char*>())    settings.mqtt.server   = doc["mqtt"]["server"].as<String>();
    if (!doc["mqtt"]["port"].isNull())              settings.mqtt.port     = doc["mqtt"]["port"].as<int>();
    if (doc["mqtt"]["user"].is<const char*>())      settings.mqtt.user     = doc["mqtt"]["user"].as<String>();
    if (doc["mqtt"]["password"].is<const char*>())  settings.mqtt.password = doc["mqtt"]["password"].as<String>();
    if (doc["mqtt"]["client_id"].is<const char*>()) settings.mqtt.client_id = doc["mqtt"]["client_id"].as<String>();
    if (doc["mqtt"]["base_topic"].is<const char*>()) settings.mqtt.base_topic = doc["mqtt"]["base_topic"].as<String>();

    // device_info
    if (doc["device_info"]["name"].is<const char*>())         settings.device_info.name         = doc["device_info"]["name"].as<String>();
    if (doc["device_info"]["manufacturer"].is<const char*>()) settings.device_info.manufacturer = doc["device_info"]["manufacturer"].as<String>();
    if (doc["device_info"]["model"].is<const char*>())        settings.device_info.model        = doc["device_info"]["model"].as<String>();

    // network
    if (doc["network"]["hostname"].is<const char*>()) settings.network.hostname     = doc["network"]["hostname"].as<String>();
    if (!doc["network"]["mdns_enabled"].isNull())     settings.network.mdns_enabled = doc["network"]["mdns_enabled"].as<bool>();

    // security
    if (!doc["security"]["auth_enabled"].isNull())         settings.security.auth_enabled         = doc["security"]["auth_enabled"].as<bool>();
    if (doc["security"]["username"].is<const char*>())     settings.security.username             = doc["security"]["username"].as<String>();
    if (doc["security"]["password"].is<const char*>())     settings.security.password             = doc["security"]["password"].as<String>();
    if (!doc["security"]["ip_whitelist_enabled"].isNull()) settings.security.ip_whitelist_enabled = doc["security"]["ip_whitelist_enabled"].as<bool>();
    settings.security.ip_ranges.clear();
    for (JsonVariantConst v : doc["security"]["ip_ranges"].as<JsonArrayConst>())
        settings.security.ip_ranges.push_back(v.as<String>());

    // rs485
    if (!doc["rs485"]["baud_rate"].isNull())        settings.rs485.baud_rate        = doc["rs485"]["baud_rate"].as<int>();
    if (!doc["rs485"]["meter_slave_addr"].isNull()) settings.rs485.meter_slave_addr = doc["rs485"]["meter_slave_addr"].as<int>();

    // pins
    if (!doc["pins"]["rs485_rx"].isNull())    settings.pins.rs485_rx    = doc["pins"]["rs485_rx"].as<int>();
    if (!doc["pins"]["rs485_tx"].isNull())    settings.pins.rs485_tx    = doc["pins"]["rs485_tx"].as<int>();
    if (!doc["pins"]["rs485_de_re"].isNull()) settings.pins.rs485_de_re = doc["pins"]["rs485_de_re"].as<int>();

    // debug
    if (!doc["debug"]["logging_enabled"].isNull())
        settings.debug.logging_enabled = doc["debug"]["logging_enabled"].as<bool>();
    if (!doc["debug"]["sensor_refresh_metrics"].isNull())
        settings.debug.sensor_refresh_metrics = doc["debug"]["sensor_refresh_metrics"].as<bool>();
    if (!doc["debug"]["raw_frame_dump"].isNull())
        settings.debug.raw_frame_dump = doc["debug"]["raw_frame_dump"].as<bool>();
    if (doc["debug"]["raw_capture_profile"].is<const char*>())
        settings.debug.raw_capture_profile = doc["debug"]["raw_capture_profile"].as<String>();

    // raw stream
    if (!doc["raw_stream"]["enabled"].isNull())
        settings.raw_stream.enabled = doc["raw_stream"]["enabled"].as<bool>();
    if (doc["raw_stream"]["host"].is<const char*>())
        settings.raw_stream.host = doc["raw_stream"]["host"].as<String>();
    if (!doc["raw_stream"]["port"].isNull())
        settings.raw_stream.port = doc["raw_stream"]["port"].as<int>();
    if (!doc["raw_stream"]["queue_kb"].isNull())
        settings.raw_stream.queue_kb = doc["raw_stream"]["queue_kb"].as<int>();
    if (!doc["raw_stream"]["reconnect_ms"].isNull())
        settings.raw_stream.reconnect_ms = doc["raw_stream"]["reconnect_ms"].as<int>();
    if (!doc["raw_stream"]["connect_timeout_ms"].isNull())
        settings.raw_stream.connect_timeout_ms = doc["raw_stream"]["connect_timeout_ms"].as<int>();
    if (!doc["raw_stream"]["serial_mirror"].isNull())
        settings.raw_stream.serial_mirror = doc["raw_stream"]["serial_mirror"].as<bool>();

    // publish tiers
    if (!doc["publish"]["tiers"]["high"]["interval_s"].isNull())
        settings.publish.tier_interval_s[TIER_HIGH]   = doc["publish"]["tiers"]["high"]["interval_s"].as<int>();
    if (!doc["publish"]["tiers"]["medium"]["interval_s"].isNull())
        settings.publish.tier_interval_s[TIER_MEDIUM] = doc["publish"]["tiers"]["medium"]["interval_s"].as<int>();
    if (!doc["publish"]["tiers"]["low"]["interval_s"].isNull())
        settings.publish.tier_interval_s[TIER_LOW]    = doc["publish"]["tiers"]["low"]["interval_s"].as<int>();

    // group tier assignments
    static const struct { const char* key; RegGroup grp; } GRP_KEY_MAP[] = {
        { "meter",           GRP_METER           },
        { "inverter_ac",     GRP_INVERTER_AC     },
        { "inverter_status", GRP_INVERTER_STATUS },
        { "inverter_energy", GRP_INVERTER_ENERGY },
        { "inverter_info",   GRP_INVERTER_INFO   },
        { "pv_strings",      GRP_PV_STRINGS      },
        { "battery",         GRP_BATTERY         },
        { "battery_u1",      GRP_BATTERY_UNIT1   },
        { "battery_u2",      GRP_BATTERY_UNIT2   },
        { "battery_packs",   GRP_BATTERY_PACKS   },
        { "battery_cfg",     GRP_BATTERY_SETTINGS},
        { "sdongle",         GRP_SDONGLE         },
    };
    for (const auto& gk : GRP_KEY_MAP) {
        if (doc["publish"]["group_tiers"][gk.key].is<const char*>()) {
            String t = doc["publish"]["group_tiers"][gk.key].as<String>();
            if      (t == "high")   settings.publish.group_tier[gk.grp] = TIER_HIGH;
            else if (t == "medium") settings.publish.group_tier[gk.grp] = TIER_MEDIUM;
            else if (t == "low")    settings.publish.group_tier[gk.grp] = TIER_LOW;
        }
    }

    // group enabled flags (omitted key keeps the default of true)
    for (const auto& gk : GRP_KEY_MAP) {
        if (!doc["publish"]["group_enabled"][gk.key].isNull())
            settings.publish.group_enabled[gk.grp] = doc["publish"]["group_enabled"][gk.key].as<bool>();
    }

    return true;
}

void ConfigManager::buildJson(JsonDocument& doc) const {
    doc["wifi"]["ssid"]     = settings.wifi.ssid;
    doc["wifi"]["password"] = settings.wifi.password;

    doc["mqtt"]["server"]             = settings.mqtt.server;
    doc["mqtt"]["port"]               = settings.mqtt.port;
    doc["mqtt"]["user"]               = settings.mqtt.user;
    doc["mqtt"]["password"]           = settings.mqtt.password;
    doc["mqtt"]["client_id"]          = settings.mqtt.client_id;
    doc["mqtt"]["base_topic"]         = settings.mqtt.base_topic;

    doc["device_info"]["name"]         = settings.device_info.name;
    doc["device_info"]["manufacturer"] = settings.device_info.manufacturer;
    doc["device_info"]["model"]        = settings.device_info.model;

    doc["network"]["hostname"]     = settings.network.hostname;
    doc["network"]["mdns_enabled"] = settings.network.mdns_enabled;

    doc["security"]["auth_enabled"]         = settings.security.auth_enabled;
    doc["security"]["username"]             = settings.security.username;
    doc["security"]["password"]             = settings.security.password;
    doc["security"]["ip_whitelist_enabled"] = settings.security.ip_whitelist_enabled;
    auto arr = doc["security"]["ip_ranges"].to<JsonArray>();
    for (const auto& r : settings.security.ip_ranges) arr.add(r);

    doc["rs485"]["baud_rate"]        = settings.rs485.baud_rate;
    doc["rs485"]["meter_slave_addr"] = settings.rs485.meter_slave_addr;

    doc["pins"]["rs485_rx"]    = settings.pins.rs485_rx;
    doc["pins"]["rs485_tx"]    = settings.pins.rs485_tx;
    doc["pins"]["rs485_de_re"] = settings.pins.rs485_de_re;

    doc["debug"]["logging_enabled"]        = settings.debug.logging_enabled;
    doc["debug"]["sensor_refresh_metrics"] = settings.debug.sensor_refresh_metrics;
    doc["debug"]["raw_frame_dump"]         = settings.debug.raw_frame_dump;
    doc["debug"]["raw_capture_profile"]    = settings.debug.raw_capture_profile;

    doc["raw_stream"]["enabled"]            = settings.raw_stream.enabled;
    doc["raw_stream"]["host"]               = settings.raw_stream.host;
    doc["raw_stream"]["port"]               = settings.raw_stream.port;
    doc["raw_stream"]["queue_kb"]           = settings.raw_stream.queue_kb;
    doc["raw_stream"]["reconnect_ms"]       = settings.raw_stream.reconnect_ms;
    doc["raw_stream"]["connect_timeout_ms"] = settings.raw_stream.connect_timeout_ms;
    doc["raw_stream"]["serial_mirror"]      = settings.raw_stream.serial_mirror;

    // publish
    static const char* TIER_NAMES[TIER_COUNT] = { "high", "medium", "low" };
    doc["publish"]["tiers"]["high"]["interval_s"]   = settings.publish.tier_interval_s[TIER_HIGH];
    doc["publish"]["tiers"]["medium"]["interval_s"] = settings.publish.tier_interval_s[TIER_MEDIUM];
    doc["publish"]["tiers"]["low"]["interval_s"]    = settings.publish.tier_interval_s[TIER_LOW];
    for (int g = 0; g < GRP_COUNT; g++) {
        uint8_t t = settings.publish.group_tier[g];
        doc["publish"]["group_tiers"][GROUP_INFO[g].key] =
            TIER_NAMES[(t < TIER_COUNT) ? t : TIER_LOW];
    }
    for (int g = 0; g < GRP_COUNT; g++)
        doc["publish"]["group_enabled"][GROUP_INFO[g].key] = settings.publish.group_enabled[g];
}

bool ConfigManager::saveConfiguration() {
    // Atomic save: write to .tmp first, then rename over the live file.
    // A power cut during write leaves the original intact.
    static const char* TMP_PATH = "/config.json.tmp";

    if (!LittleFS.begin(false)) {
        UnifiedLogger::error("[CFG] LittleFS mount failed — cannot save\n");
        return false;
    }

    File f = LittleFS.open(TMP_PATH, "w");
    if (!f) {
        UnifiedLogger::error("[CFG] cannot open config.json.tmp for writing\n");
        return false;
    }

    JsonDocument doc;
    buildJson(doc);
    size_t written = serializeJsonPretty(doc, f);
    f.close();

    if (written == 0) {
        LittleFS.remove(TMP_PATH);
        UnifiedLogger::error("[CFG] config.json.tmp write failed — original untouched\n");
        return false;
    }

    // Versioned backup rotation — keep 2 rolling backups on LittleFS:
    //   config.json → config_bak1.json → config_bak2.json
    // A power cut while writing leaves the previous config recoverable.
    if (LittleFS.exists("/config_bak1.json")) {
        LittleFS.remove("/config_bak2.json");
        LittleFS.rename("/config_bak1.json", "/config_bak2.json");
    }
    if (LittleFS.exists(CONFIG_PATH)) {
        LittleFS.rename(CONFIG_PATH, "/config_bak1.json");
    }

    if (!LittleFS.rename(TMP_PATH, CONFIG_PATH)) {
        UnifiedLogger::error("[CFG] rename .tmp → config.json failed\n");
        return false;
    }

    UnifiedLogger::info("[CFG] saved atomically (%u bytes)\n", (unsigned)written);
    return true;
}

String ConfigManager::getSettingsJson() const {
    JsonDocument doc;
    buildJson(doc);
    // Don't expose passwords in the API response
    doc["wifi"]["password"]    = "";
    doc["mqtt"]["password"]    = "";
    doc["security"]["password"] = "";
    String out;
    serializeJson(doc, out);
    return out;
}

bool ConfigManager::updateSettingsFromJson(const String& jsonBody) {
    PsramJsonAllocator alloc;
    JsonDocument doc(&alloc);
    DeserializationError derr = deserializeJson(doc, jsonBody);
    if (derr != DeserializationError::Ok) {
        UnifiedLogger::error("[CFG] updateSettingsFromJson: parse error: %s (body len=%u)\n",
                             derr.c_str(), (unsigned)jsonBody.length());
        return false;
    }

    // Helper: only update if key present AND non-empty (passwords: never overwrite with empty)
    auto ss = [&](const char* k1, const char* k2, String& target) {
        if (doc[k1][k2].is<const char*>()) target = doc[k1][k2].as<String>();
    };
    auto sp = [&](const char* k1, const char* k2, String& target) {
        // Password variant: only update when non-empty (empty = "keep current")
        if (doc[k1][k2].is<const char*>()) {
            String v = doc[k1][k2].as<String>();
            if (v.length() > 0) target = v;
        }
    };
    auto si = [&](const char* k1, const char* k2, int& target) {
        if (!doc[k1][k2].isNull()) target = doc[k1][k2].as<int>();
    };
    auto sb = [&](const char* k1, const char* k2, bool& target) {
        if (!doc[k1][k2].isNull()) target = doc[k1][k2].as<bool>();
    };

    ss("wifi","ssid",     settings.wifi.ssid);
    sp("wifi","password", settings.wifi.password);  // sp: empty = keep current

    ss("mqtt","server",    settings.mqtt.server);
    si("mqtt","port",      settings.mqtt.port);
    ss("mqtt","user",      settings.mqtt.user);
    sp("mqtt","password",  settings.mqtt.password);  // sp: empty = keep current
    ss("mqtt","client_id", settings.mqtt.client_id);
    ss("mqtt","base_topic",settings.mqtt.base_topic);

    ss("device_info","name",         settings.device_info.name);
    ss("device_info","manufacturer", settings.device_info.manufacturer);
    ss("device_info","model",        settings.device_info.model);

    ss("network","hostname", settings.network.hostname);
    sb("network","mdns_enabled", settings.network.mdns_enabled);

    sb("security","auth_enabled",         settings.security.auth_enabled);
    ss("security","username",             settings.security.username);
    sp("security","password",             settings.security.password);  // sp: empty = keep current
    sb("security","ip_whitelist_enabled", settings.security.ip_whitelist_enabled);
    if (!doc["security"]["ip_ranges"].isNull()) {
        settings.security.ip_ranges.clear();
        for (JsonVariant v : doc["security"]["ip_ranges"].as<JsonArray>())
            settings.security.ip_ranges.push_back(v.as<String>());
    }

    si("rs485","baud_rate",        settings.rs485.baud_rate);
    si("rs485","meter_slave_addr", settings.rs485.meter_slave_addr);

    si("pins","rs485_rx",    settings.pins.rs485_rx);
    si("pins","rs485_tx",    settings.pins.rs485_tx);
    si("pins","rs485_de_re", settings.pins.rs485_de_re);

    sb("debug","logging_enabled",        settings.debug.logging_enabled);
    sb("debug","sensor_refresh_metrics", settings.debug.sensor_refresh_metrics);
    sb("debug","raw_frame_dump",         settings.debug.raw_frame_dump);
    ss("debug","raw_capture_profile",    settings.debug.raw_capture_profile);
    sb("raw_stream","enabled",            settings.raw_stream.enabled);
    ss("raw_stream","host",               settings.raw_stream.host);
    si("raw_stream","port",               settings.raw_stream.port);
    si("raw_stream","queue_kb",           settings.raw_stream.queue_kb);
    si("raw_stream","reconnect_ms",       settings.raw_stream.reconnect_ms);
    si("raw_stream","connect_timeout_ms", settings.raw_stream.connect_timeout_ms);
    sb("raw_stream","serial_mirror",      settings.raw_stream.serial_mirror);
    // Apply logging toggle immediately — no reboot required
    UnifiedLogger::setEnabled(settings.debug.logging_enabled);

    // publish tiers (live update — takes effect on next mqtt_tick)
    if (!doc["publish"]["tiers"]["high"]["interval_s"].isNull())
        settings.publish.tier_interval_s[TIER_HIGH]   = doc["publish"]["tiers"]["high"]["interval_s"].as<int>();
    if (!doc["publish"]["tiers"]["medium"]["interval_s"].isNull())
        settings.publish.tier_interval_s[TIER_MEDIUM] = doc["publish"]["tiers"]["medium"]["interval_s"].as<int>();
    if (!doc["publish"]["tiers"]["low"]["interval_s"].isNull())
        settings.publish.tier_interval_s[TIER_LOW]    = doc["publish"]["tiers"]["low"]["interval_s"].as<int>();

    // group tier assignments
    static const struct { const char* key; RegGroup grp; } GRP_KEY_MAP[] = {
        { "meter",           GRP_METER           },
        { "inverter_ac",     GRP_INVERTER_AC     },
        { "inverter_status", GRP_INVERTER_STATUS },
        { "inverter_energy", GRP_INVERTER_ENERGY },
        { "inverter_info",   GRP_INVERTER_INFO   },
        { "pv_strings",      GRP_PV_STRINGS      },
        { "battery",         GRP_BATTERY         },
        { "battery_u1",      GRP_BATTERY_UNIT1   },
        { "battery_u2",      GRP_BATTERY_UNIT2   },
        { "battery_packs",   GRP_BATTERY_PACKS   },
        { "battery_cfg",     GRP_BATTERY_SETTINGS},
        { "sdongle",         GRP_SDONGLE         },
    };
    if (!doc["publish"]["group_tiers"].isNull()) {
        for (const auto& gk : GRP_KEY_MAP) {
            if (doc["publish"]["group_tiers"][gk.key].is<const char*>()) {
                String t = doc["publish"]["group_tiers"][gk.key].as<String>();
                if      (t == "high")   settings.publish.group_tier[gk.grp] = TIER_HIGH;
                else if (t == "medium") settings.publish.group_tier[gk.grp] = TIER_MEDIUM;
                else if (t == "low")    settings.publish.group_tier[gk.grp] = TIER_LOW;
            }
        }
    }
    if (!doc["publish"]["group_enabled"].isNull()) {
        for (const auto& gk : GRP_KEY_MAP) {
            if (!doc["publish"]["group_enabled"][gk.key].isNull())
                settings.publish.group_enabled[gk.grp] = doc["publish"]["group_enabled"][gk.key].as<bool>();
        }
    }

    return saveConfiguration();
}
