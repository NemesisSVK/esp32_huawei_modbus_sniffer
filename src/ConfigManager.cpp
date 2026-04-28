#include "ConfigManager.h"
#include "huawei_decoder.h"
#include "UnifiedLogger.h"
#include "PsramJsonAllocator.h"
#include <Arduino.h>

#define CONFIG_PATH "/config.json"

static String normalize_manual_selector_source(String src) {
    src.trim();
    src.toLowerCase();
    src.replace("-", "_");
    if (src == "fc03" || src == "fc04" || src == "h41_33" || src == "h41_x")
        return src;
    return "";
}

static void normalize_raw_capture_profile(String& profile) {
    if (profile == "unknown_h41" || profile == "all_frames") return;
    profile = "unknown_h41";
}

static bool normalize_manual_group_selector(const String& input,
                                            String* out_selector,
                                            String* out_register,
                                            String* out_error) {
    String s = input;
    s.trim();
    if (s.length() == 0) {
        if (out_error) *out_error = "Manual group selector cannot be empty";
        return false;
    }

    String reg_name = s;
    const int sep = s.indexOf(':');
    if (sep >= 0) {
        if (sep == 0) {
            if (out_error) *out_error = "Manual group source selector is missing source token before ':'";
            return false;
        }
        String src = s.substring(0, sep);
        reg_name = s.substring(sep + 1);
        reg_name.trim();
        if (reg_name.length() == 0) {
            if (out_error) *out_error = "Manual group source selector is missing register name after ':'";
            return false;
        }
        String norm_src = normalize_manual_selector_source(src);
        if (norm_src.length() == 0) {
            if (out_error) *out_error = "Unknown manual-group source token (expected fc03|fc04|h41_33|h41_x)";
            return false;
        }
        s = norm_src + ":" + reg_name;
    }

    if (!huawei_decoder_is_known_register_name(reg_name.c_str())) {
        if (out_error) *out_error = String("Unknown register in manual group: '") + reg_name + "'";
        return false;
    }

    if (out_selector) *out_selector = s;
    if (out_register) *out_register = reg_name;
    return true;
}

static bool validate_runtime_settings(const Settings& st, String* out_error) {
    auto fail = [&](const String& msg) -> bool {
        if (out_error) *out_error = msg;
        return false;
    };

    if (st.mqtt.port < 1 || st.mqtt.port > 65535)
        return fail("mqtt.port must be 1-65535");

    if (st.rs485.baud_rate <= 0)
        return fail("rs485.baud_rate must be a positive integer");
    if (st.rs485.meter_slave_addr < 1 || st.rs485.meter_slave_addr > 247)
        return fail("rs485.meter_slave_addr must be 1-247");

    if (st.pins.rs485_rx < 0)
        return fail("pins.rs485_rx must be a non-negative GPIO number");
    if (st.pins.rs485_tx < 0)
        return fail("pins.rs485_tx must be a non-negative GPIO number");
    if (st.pins.rs485_de_re < -1)
        return fail("pins.rs485_de_re must be -1 (auto-control) or a non-negative GPIO number");

    if (st.security.auth_enabled) {
        String user = st.security.username;
        user.trim();
        if (user.length() == 0)
            return fail("security.username is required when auth_enabled is true");
        if (st.security.password.length() == 0)
            return fail("security.password is required when auth_enabled is true");
    }

    if (st.security.ip_ranges.size() > 50)
        return fail("security.ip_ranges cannot exceed 50 entries");
    for (size_t i = 0; i < st.security.ip_ranges.size(); i++) {
        String range = st.security.ip_ranges[i];
        range.trim();
        if (range.length() == 0)
            return fail(String("security.ip_ranges[") + i + "] must not be empty");
        if (!IPWhitelistManager::isValidIPRange(range))
            return fail(String("security.ip_ranges[") + i + "] is not a valid IP or range");
    }

    if (st.debug.raw_capture_profile != "unknown_h41" &&
        st.debug.raw_capture_profile != "all_frames")
        return fail("debug.raw_capture_profile must be unknown_h41 or all_frames");

    if (st.raw_stream.port < 1 || st.raw_stream.port > 65535)
        return fail("raw_stream.port must be 1-65535");
    if (st.raw_stream.queue_kb < 32 || st.raw_stream.queue_kb > 2048)
        return fail("raw_stream.queue_kb must be 32-2048");
    if (st.raw_stream.reconnect_ms < 100 || st.raw_stream.reconnect_ms > 60000)
        return fail("raw_stream.reconnect_ms must be 100-60000");
    if (st.raw_stream.connect_timeout_ms < 100 || st.raw_stream.connect_timeout_ms > 30000)
        return fail("raw_stream.connect_timeout_ms must be 100-30000");
    if (st.raw_stream.enabled) {
        String host = st.raw_stream.host;
        host.trim();
        if (host.length() == 0)
            return fail("raw_stream.host is required when raw_stream.enabled is true");
    }

    for (int t = 0; t < TIER_COUNT; t++) {
        if (st.publish.tier_interval_s[t] < 0)
            return fail("publish.tiers.*.interval_s must be a non-negative integer");
    }
    for (int g = 0; g < GRP_COUNT; g++) {
        if (st.publish.group_tier[g] >= TIER_COUNT)
            return fail("publish.group_tiers contains an invalid tier value");
    }

    if (st.publish.manual_group.tier >= TIER_COUNT)
        return fail("publish.manual_group.tier must be high|medium|low");
    if (st.publish.manual_group.registers.size() > 64)
        return fail("publish.manual_group.registers supports max 64 entries");
    for (size_t i = 0; i < st.publish.manual_group.registers.size(); i++) {
        String selector, reg_name, err;
        if (!normalize_manual_group_selector(st.publish.manual_group.registers[i], &selector, &reg_name, &err)) {
            if (err.length() == 0)
                err = "Invalid manual-group selector";
            return fail(err);
        }
    }

    return true;
}

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
    settings.publish.manual_group.enabled = false;
    settings.publish.manual_group.tier = TIER_HIGH;
    settings.publish.manual_group.registers.clear();
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
    normalize_raw_capture_profile(settings.debug.raw_capture_profile);

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
        { "priority_manual", GRP_PRIORITY_MANUAL },
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

    // manual priority group
    if (!doc["publish"]["manual_group"]["enabled"].isNull())
        settings.publish.manual_group.enabled = doc["publish"]["manual_group"]["enabled"].as<bool>();
    if (doc["publish"]["manual_group"]["tier"].is<const char*>()) {
        String t = doc["publish"]["manual_group"]["tier"].as<String>();
        if      (t == "high")   settings.publish.manual_group.tier = TIER_HIGH;
        else if (t == "medium") settings.publish.manual_group.tier = TIER_MEDIUM;
        else if (t == "low")    settings.publish.manual_group.tier = TIER_LOW;
    }
    settings.publish.manual_group.registers.clear();
    if (doc["publish"]["manual_group"]["registers"].is<JsonArrayConst>()) {
        for (JsonVariantConst v : doc["publish"]["manual_group"]["registers"].as<JsonArrayConst>()) {
            if (!v.is<const char*>()) continue;
            String selector, reg_name;
            if (!normalize_manual_group_selector(v.as<String>(), &selector, &reg_name, nullptr)) continue;
            bool exists = false;
            for (const String& cur : settings.publish.manual_group.registers) {
                if (cur == selector) { exists = true; break; }
            }
            if (!exists) settings.publish.manual_group.registers.push_back(selector);
            if (settings.publish.manual_group.registers.size() >= 64) break;
        }
    }
    settings.publish.group_tier[GRP_PRIORITY_MANUAL] = settings.publish.manual_group.tier;
    settings.publish.group_enabled[GRP_PRIORITY_MANUAL] = settings.publish.manual_group.enabled;

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
    doc["publish"]["manual_group"]["enabled"] = settings.publish.manual_group.enabled;
    doc["publish"]["manual_group"]["tier"] =
        TIER_NAMES[(settings.publish.manual_group.tier < TIER_COUNT)
            ? settings.publish.manual_group.tier : TIER_HIGH];
    JsonArray mgRegs = doc["publish"]["manual_group"]["registers"].to<JsonArray>();
    for (const String& reg : settings.publish.manual_group.registers)
        mgRegs.add(reg);
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

String ConfigManager::getSettingsJsonPretty(bool redactPasswords) const {
    JsonDocument doc;
    buildJson(doc);
    if (redactPasswords) {
        doc["wifi"]["password"]     = "";
        doc["mqtt"]["password"]     = "";
        doc["security"]["password"] = "";
    }
    String out;
    serializeJsonPretty(doc, out);
    // Match project file style more closely on Windows.
    out.replace("\n", "\r\n");
    if (!out.endsWith("\r\n")) out += "\r\n";
    return out;
}

bool ConfigManager::updateSettingsFromJson(const String& jsonBody) {
    lastError = "";
    PsramJsonAllocator alloc;
    JsonDocument doc(&alloc);
    DeserializationError derr = deserializeJson(doc, jsonBody);
    if (derr != DeserializationError::Ok) {
        UnifiedLogger::error("[CFG] updateSettingsFromJson: parse error: %s (body len=%u)\n",
                             derr.c_str(), (unsigned)jsonBody.length());
        lastError = String("JSON parse error: ") + derr.c_str();
        return false;
    }

    Settings candidate = settings;
    Settings previous = settings;

    // Helpers with strict type checks.
    auto setString = [&](const char* k1, const char* k2, String& target, const char* fqKey) -> bool {
        JsonVariantConst v = doc[k1][k2];
        if (v.isNull()) return true;
        if (!v.is<const char*>()) {
            lastError = String(fqKey) + " must be a string";
            return false;
        }
        target = v.as<String>();
        return true;
    };
    auto setPassword = [&](const char* k1, const char* k2, String& target, const char* fqKey) -> bool {
        JsonVariantConst v = doc[k1][k2];
        if (v.isNull()) return true;
        if (!v.is<const char*>()) {
            lastError = String(fqKey) + " must be a string";
            return false;
        }
        String s = v.as<String>();
        if (s.length() > 0) target = s; // empty means keep current
        return true;
    };
    auto setInt = [&](const char* k1, const char* k2, int& target, const char* fqKey) -> bool {
        JsonVariantConst v = doc[k1][k2];
        if (v.isNull()) return true;
        if (!(v.is<int>() || v.is<long>())) {
            lastError = String(fqKey) + " must be an integer";
            return false;
        }
        target = v.as<int>();
        return true;
    };
    auto setBool = [&](const char* k1, const char* k2, bool& target, const char* fqKey) -> bool {
        JsonVariantConst v = doc[k1][k2];
        if (v.isNull()) return true;
        if (!v.is<bool>()) {
            lastError = String(fqKey) + " must be a boolean";
            return false;
        }
        target = v.as<bool>();
        return true;
    };

    if (!setString("wifi","ssid", candidate.wifi.ssid, "wifi.ssid")) return false;
    if (!setPassword("wifi","password", candidate.wifi.password, "wifi.password")) return false;

    if (!setString("mqtt","server", candidate.mqtt.server, "mqtt.server")) return false;
    if (!setInt("mqtt","port", candidate.mqtt.port, "mqtt.port")) return false;
    if (!setString("mqtt","user", candidate.mqtt.user, "mqtt.user")) return false;
    if (!setPassword("mqtt","password", candidate.mqtt.password, "mqtt.password")) return false;
    if (!setString("mqtt","client_id", candidate.mqtt.client_id, "mqtt.client_id")) return false;
    if (!setString("mqtt","base_topic", candidate.mqtt.base_topic, "mqtt.base_topic")) return false;

    if (!setString("device_info","name", candidate.device_info.name, "device_info.name")) return false;
    if (!setString("device_info","manufacturer", candidate.device_info.manufacturer, "device_info.manufacturer")) return false;
    if (!setString("device_info","model", candidate.device_info.model, "device_info.model")) return false;

    if (!setString("network","hostname", candidate.network.hostname, "network.hostname")) return false;
    if (!setBool("network","mdns_enabled", candidate.network.mdns_enabled, "network.mdns_enabled")) return false;

    if (!setBool("security","auth_enabled", candidate.security.auth_enabled, "security.auth_enabled")) return false;
    if (!setString("security","username", candidate.security.username, "security.username")) return false;
    if (!setPassword("security","password", candidate.security.password, "security.password")) return false;
    if (!setBool("security","ip_whitelist_enabled", candidate.security.ip_whitelist_enabled, "security.ip_whitelist_enabled")) return false;
    if (!doc["security"]["ip_ranges"].isNull()) {
        JsonVariantConst ipRanges = doc["security"]["ip_ranges"];
        if (!ipRanges.is<JsonArrayConst>()) {
            lastError = "security.ip_ranges must be an array";
            return false;
        }
        candidate.security.ip_ranges.clear();
        size_t idxRange = 0;
        for (JsonVariantConst v : ipRanges.as<JsonArrayConst>()) {
            if (!v.is<const char*>()) {
                lastError = String("security.ip_ranges[") + idxRange + "] must be a string";
                return false;
            }
            String range = v.as<String>();
            range.trim();
            candidate.security.ip_ranges.push_back(range);
            idxRange++;
        }
    }

    if (!setInt("rs485","baud_rate", candidate.rs485.baud_rate, "rs485.baud_rate")) return false;
    if (!setInt("rs485","meter_slave_addr", candidate.rs485.meter_slave_addr, "rs485.meter_slave_addr")) return false;

    if (!setInt("pins","rs485_rx", candidate.pins.rs485_rx, "pins.rs485_rx")) return false;
    if (!setInt("pins","rs485_tx", candidate.pins.rs485_tx, "pins.rs485_tx")) return false;
    if (!setInt("pins","rs485_de_re", candidate.pins.rs485_de_re, "pins.rs485_de_re")) return false;

    if (!setBool("debug","logging_enabled", candidate.debug.logging_enabled, "debug.logging_enabled")) return false;
    if (!setBool("debug","sensor_refresh_metrics", candidate.debug.sensor_refresh_metrics, "debug.sensor_refresh_metrics")) return false;
    if (!setBool("debug","raw_frame_dump", candidate.debug.raw_frame_dump, "debug.raw_frame_dump")) return false;
    if (!setString("debug","raw_capture_profile", candidate.debug.raw_capture_profile, "debug.raw_capture_profile")) return false;
    normalize_raw_capture_profile(candidate.debug.raw_capture_profile);

    if (!setBool("raw_stream","enabled", candidate.raw_stream.enabled, "raw_stream.enabled")) return false;
    if (!setString("raw_stream","host", candidate.raw_stream.host, "raw_stream.host")) return false;
    if (!setInt("raw_stream","port", candidate.raw_stream.port, "raw_stream.port")) return false;
    if (!setInt("raw_stream","queue_kb", candidate.raw_stream.queue_kb, "raw_stream.queue_kb")) return false;
    if (!setInt("raw_stream","reconnect_ms", candidate.raw_stream.reconnect_ms, "raw_stream.reconnect_ms")) return false;
    if (!setInt("raw_stream","connect_timeout_ms", candidate.raw_stream.connect_timeout_ms, "raw_stream.connect_timeout_ms")) return false;
    if (!setBool("raw_stream","serial_mirror", candidate.raw_stream.serial_mirror, "raw_stream.serial_mirror")) return false;

    // publish tiers (live update; takes effect on next mqtt_tick)
    if (!doc["publish"]["tiers"]["high"]["interval_s"].isNull()) {
        JsonVariantConst v = doc["publish"]["tiers"]["high"]["interval_s"];
        if (!(v.is<int>() || v.is<long>())) {
            lastError = "publish.tiers.high.interval_s must be an integer";
            return false;
        }
        candidate.publish.tier_interval_s[TIER_HIGH] = v.as<int>();
    }
    if (!doc["publish"]["tiers"]["medium"]["interval_s"].isNull()) {
        JsonVariantConst v = doc["publish"]["tiers"]["medium"]["interval_s"];
        if (!(v.is<int>() || v.is<long>())) {
            lastError = "publish.tiers.medium.interval_s must be an integer";
            return false;
        }
        candidate.publish.tier_interval_s[TIER_MEDIUM] = v.as<int>();
    }
    if (!doc["publish"]["tiers"]["low"]["interval_s"].isNull()) {
        JsonVariantConst v = doc["publish"]["tiers"]["low"]["interval_s"];
        if (!(v.is<int>() || v.is<long>())) {
            lastError = "publish.tiers.low.interval_s must be an integer";
            return false;
        }
        candidate.publish.tier_interval_s[TIER_LOW] = v.as<int>();
    }

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
        { "priority_manual", GRP_PRIORITY_MANUAL },
    };

    if (!doc["publish"]["group_tiers"].isNull()) {
        if (!doc["publish"]["group_tiers"].is<JsonObjectConst>()) {
            lastError = "publish.group_tiers must be an object";
            return false;
        }
        for (const auto& gk : GRP_KEY_MAP) {
            if (doc["publish"]["group_tiers"][gk.key].is<const char*>()) {
                String t = doc["publish"]["group_tiers"][gk.key].as<String>();
                if      (t == "high")   candidate.publish.group_tier[gk.grp] = TIER_HIGH;
                else if (t == "medium") candidate.publish.group_tier[gk.grp] = TIER_MEDIUM;
                else if (t == "low")    candidate.publish.group_tier[gk.grp] = TIER_LOW;
                else {
                    lastError = String("publish.group_tiers.") + gk.key + " must be high|medium|low";
                    return false;
                }
            } else if (!doc["publish"]["group_tiers"][gk.key].isNull()) {
                lastError = String("publish.group_tiers.") + gk.key + " must be a string";
                return false;
            }
        }
    }

    if (!doc["publish"]["group_enabled"].isNull()) {
        if (!doc["publish"]["group_enabled"].is<JsonObjectConst>()) {
            lastError = "publish.group_enabled must be an object";
            return false;
        }
        for (const auto& gk : GRP_KEY_MAP) {
            JsonVariantConst v = doc["publish"]["group_enabled"][gk.key];
            if (!v.isNull()) {
                if (!v.is<bool>()) {
                    lastError = String("publish.group_enabled.") + gk.key + " must be a boolean";
                    return false;
                }
                candidate.publish.group_enabled[gk.grp] = v.as<bool>();
            }
        }
    }

    if (!doc["publish"]["manual_group"].isNull()) {
        JsonVariantConst mg = doc["publish"]["manual_group"];
        if (!mg.is<JsonObjectConst>()) {
            lastError = "publish.manual_group must be an object";
            return false;
        }

        if (!mg["enabled"].isNull()) {
            if (!mg["enabled"].is<bool>()) {
                lastError = "publish.manual_group.enabled must be a boolean";
                return false;
            }
            candidate.publish.manual_group.enabled = mg["enabled"].as<bool>();
        }

        if (mg["tier"].is<const char*>()) {
            String t = mg["tier"].as<String>();
            if      (t == "high")   candidate.publish.manual_group.tier = TIER_HIGH;
            else if (t == "medium") candidate.publish.manual_group.tier = TIER_MEDIUM;
            else if (t == "low")    candidate.publish.manual_group.tier = TIER_LOW;
            else {
                lastError = String("publish.manual_group.tier must be high|medium|low (got '") + t + "')";
                return false;
            }
        } else if (!mg["tier"].isNull()) {
            lastError = "publish.manual_group.tier must be a string";
            return false;
        }

        if (!mg["registers"].isNull()) {
            if (!mg["registers"].is<JsonArrayConst>()) {
                lastError = "publish.manual_group.registers must be an array of selectors (register or source:register)";
                return false;
            }
            std::vector<String> regs;
            for (JsonVariantConst v : mg["registers"].as<JsonArrayConst>()) {
                if (!v.is<const char*>()) {
                    lastError = "publish.manual_group.registers entries must be strings";
                    return false;
                }
                String selector, reg_name, sel_err;
                if (!normalize_manual_group_selector(v.as<String>(), &selector, &reg_name, &sel_err)) {
                    if (sel_err.length() == 0)
                        sel_err = "Invalid manual-group selector";
                    lastError = sel_err;
                    return false;
                }
                bool exists = false;
                for (const String& cur : regs) {
                    if (cur == selector) { exists = true; break; }
                }
                if (!exists) regs.push_back(selector);
                if (regs.size() > 64) {
                    lastError = "publish.manual_group.registers supports max 64 entries";
                    return false;
                }
            }
            candidate.publish.manual_group.registers = regs;
        }
    }

    candidate.publish.group_tier[GRP_PRIORITY_MANUAL] = candidate.publish.manual_group.tier;
    candidate.publish.group_enabled[GRP_PRIORITY_MANUAL] = candidate.publish.manual_group.enabled;

    if (!validate_runtime_settings(candidate, &lastError))
        return false;

    settings = candidate;
    UnifiedLogger::setEnabled(settings.debug.logging_enabled);

    if (!saveConfiguration()) {
        settings = previous; // rollback in-memory state if persistence fails
        UnifiedLogger::setEnabled(settings.debug.logging_enabled);
        lastError = "Failed to save config to LittleFS";
        return false;
    }

    return true;
}
