#pragma once
#include "ConfigManager.h"
#include "MQTTManager.h"
#include "OTAManager.h"
#include "IPWhitelistManager.h"
#include "reg_groups.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

/**
 * Initialize and start the async web server.
 * Call after WiFi is connected.
 * ap_mode = true when WiFi failed and we're running a captive AP (optional use).
 */
void web_ui_init(ConfigManager* cfg, MQTTManager* mqtt, OTAManager* ota, bool ap_mode = false);

/** Must be called from loop() — handles any pending housekeeping. */
void web_ui_loop();

// Group detection state — s_seen[] is runtime-only (resets on boot).
// Enabled state reads from Settings::Publish::group_enabled[] (config.json).
// All configuration is managed through the settings page (POST /api/config).
bool     group_is_enabled(RegGroup g);   // reads config.json publish.group_enabled
bool     group_is_seen(RegGroup g);      // runtime detection flag
void     group_mark_seen(RegGroup g);    // called from huawei_decoder on first decode
void     group_set_enabled(RegGroup g, bool en);  // no-op — use settings page

// Frame counter incremented by sniffer task; read by web UI status
extern volatile uint32_t g_frames_decoded;
