/**
 * main.cpp — Huawei Solar Modbus RTU Passive Sniffer
 *
 * Boot sequence:
 *   1. ConfigManager — load config.json from LittleFS (or use defaults)
 *   2. UART RS-485    — baud rate from config
 *   3. Huawei decoder — register callbacks to mqtt_publisher
 *   4. WiFiManager    — non-blocking connect
 *   5. mDNS           — hostname.local
 *   6. MQTTManager    — setup but don't block waiting for MQTT
 *   7. Web UI         — AsyncWebServer on port 80
 *   8. Sniffer task   — FreeRTOS task on core 1, priority 5
 *
 * Hardware:
 *   ESP32-S3 R16N8 + "TTL to RS485 Hardware Auto Control" (MAX485, auto DE/RE)
 *   RS-485 A/B tapped in parallel — sniffer never transmits.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "config.h"
#include "ConfigManager.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "OTAManager.h"
#include "UnifiedLogger.h"
#include "modbus_rtu.h"
#include "huawei_decoder.h"
#include "mqtt_publisher.h"
#include "LiveValueStore.h"
#include "web_ui.h"
#include "reg_groups.h"
#include "RawFrameStreamer.h"
#include "RGBLedManager.h"
#include "ChipTempSensorManager.h"
#include "PsramLimits.h"

// ============================================================
// Global manager instances
// ============================================================
static ConfigManager         g_config;
static WiFiManager           g_wifi;
static MQTTManager           g_mqtt;
static OTAManager            g_ota;
static RGBLedManager         g_led(LED_BUILTIN_PIN);
static ChipTempSensorManager g_chiptemp;

static void on_decoded_value(const char* name, float value,
                             const char* unit, uint8_t slave_addr,
                             RegGroup group, uint8_t source_id,
                             uint16_t reg_addr, uint8_t reg_words) {
    mqtt_publish_value(name, value, unit, slave_addr, group, source_id, reg_addr, reg_words);
    live_value_store_publish_known(name, value, unit, slave_addr, group, source_id, reg_addr, reg_words);
}

// ============================================================
// Inter-frame gap — calculated from baud rate
// ============================================================
static uint32_t s_frame_gap_us = 4000;

static uint32_t calc_gap_us(uint32_t baud) {
    return (uint32_t)((3.5f * 10.0f * 1e6f / (float)baud) * 1.2f);
}

// ============================================================
// UART / Sniffer task
// ============================================================
static uint8_t     s_rx_frame[MODBUS_MAX_FRAME_LEN];
static uint16_t    s_rx_len = 0;
static TaskHandle_t s_sniffer_task = nullptr;

static void uart_init_rs485() {
    const Settings& st = g_config.getSettings();
    uint32_t baud = (uint32_t)st.rs485.baud_rate;
    s_frame_gap_us = calc_gap_us(baud);

    uart_config_t cfg = {
        .baud_rate           = (int)baud,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
    };
    ESP_ERROR_CHECK(uart_driver_install(RS485_UART_NUM, UART_RX_BUF_SIZE, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(RS485_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(RS485_UART_NUM,
                                   st.pins.rs485_tx, st.pins.rs485_rx,
                                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_mode(RS485_UART_NUM, UART_MODE_UART));
    uart_set_rx_timeout(RS485_UART_NUM, 3);

    UnifiedLogger::info("[UART] %u baud, RX=GPIO%d TX=GPIO%d, gap=%uus\n",
                        baud, st.pins.rs485_rx, st.pins.rs485_tx, s_frame_gap_us);
}

static void process_raw_frame(const uint8_t* buf, uint16_t len) {
    if (len < MODBUS_RTU_MIN_FRAME) return;
    ModbusFrame frame;
    if (!modbus_parse_frame(buf, len, &frame)) {
        UnifiedLogger::verbose("[SNIF] CRC FAIL len=%u\n", len);
        return;
    }
    g_frames_decoded++;
    if (frame.type == FRAME_RESPONSE)
        UnifiedLogger::info("[SNIF] RSP slave=0x%02X regs=%u\n",
                            frame.slave_addr, frame.rsp_reg_count);
    huawei_decoder_feed(&frame);
    // Note: mqtt_flush() removed — publication is now driven by mqtt_tick() in loop()
}

static void sniffer_task(void* /*pv*/) {
    // Register this task with the hardware watchdog
    esp_task_wdt_add(NULL);

    uint8_t  byte_buf[1];
    uint64_t last_byte_us = 0;
    for (;;) {
        esp_task_wdt_reset();  // feed watchdog every loop
        int n = uart_read_bytes(RS485_UART_NUM, byte_buf, 1, pdMS_TO_TICKS(2));
        uint64_t now = esp_timer_get_time();
        if (n > 0) {
            if (s_rx_len > 0 && (now - last_byte_us) >= s_frame_gap_us) {
                process_raw_frame(s_rx_frame, s_rx_len);
                s_rx_len = 0;
            }
            if (s_rx_len < MODBUS_MAX_FRAME_LEN)
                s_rx_frame[s_rx_len++] = byte_buf[0];
            else {
                s_rx_len = 0;
                s_rx_frame[s_rx_len++] = byte_buf[0];
            }
            last_byte_us = now;
        } else {
            if (s_rx_len > 0 && (now - last_byte_us) >= s_frame_gap_us) {
                process_raw_frame(s_rx_frame, s_rx_len);
                s_rx_len = 0;
            }
        }
        taskYIELD();
    }
}

// ============================================================
// mDNS
// ============================================================
static void mdns_start() {
    const String& host = g_config.getSettings().network.hostname;
    if (!g_config.getSettings().network.mdns_enabled) return;
    if (!MDNS.begin(host.c_str())) {
        UnifiedLogger::error("[mDNS] start failed\n");
        return;
    }
    MDNS.addService("http", "tcp", 80);
    UnifiedLogger::info("[mDNS] http://%s.local/\n", host.c_str());
}

// ============================================================
// Arduino entry points
// ============================================================
void setup() {
    // LED on first — BLUE = booting. NeoPixel works before Serial is ready.
    g_led.begin();

    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== Huawei Solar Modbus RTU Sniffer ===");

    // 0. Logger — always on at boot so early messages reach Serial;
    //            after config loads we apply debug.logging_enabled from config.json
    UnifiedLogger::begin(true);

    // 1. Config
    if (!g_config.loadConfiguration()) {
        UnifiedLogger::warning("[MAIN] Using built-in defaults\n");
    }
    // Apply the persisted logging preference (no-op if config not loaded — stays true)
    UnifiedLogger::setEnabled(g_config.getSettings().debug.logging_enabled);
    g_chiptemp.begin();
    const Settings& st = g_config.getSettings();

    // 1b. Hardware watchdog — 30s timeout, triggers panic + reboot.
    //     Only the sniffer task (Core 1) is registered: ArduinoOTA.handle() can
    //     block the loop task for 40+ seconds during a large filesystem transfer,
    //     which would trip the TWDT.  The sniffer task registers itself below.
    esp_task_wdt_init(30, true);   // timeout_s=30, panic=true
    // NOTE: do NOT call esp_task_wdt_add(NULL) here for the loop task.

    // 1c. OTA
    g_ota.setHostname(st.network.hostname);
    g_ota.refreshConfigStatus();  // loads /ota.json so the web UI can show its status

    // 2. DE/RE pin (auto-control boards: rs485_de_re = -1)
    if (st.pins.rs485_de_re >= 0) {
        pinMode(st.pins.rs485_de_re, OUTPUT);
        digitalWrite(st.pins.rs485_de_re, LOW);
        UnifiedLogger::info("[HW] DE/RE GPIO%d held LOW\n", st.pins.rs485_de_re);
    } else {
        UnifiedLogger::info("[HW] Auto-control board — no DE/RE pin needed\n");
    }

    // 3. UART
    uart_init_rs485();

    // 4. Decoder — callback publishes to MQTT and Live Modbus store
    live_value_store_init();
    huawei_decoder_init(on_decoded_value);
    huawei_decoder_set_raw_dump(st.debug.raw_frame_dump);
    huawei_decoder_set_raw_capture_profile(st.debug.raw_capture_profile.c_str());
    RawFrameStreamConfig rs_cfg;
    rs_cfg.enabled = st.raw_stream.enabled;
    rs_cfg.host = st.raw_stream.host.c_str();
    rs_cfg.port = (uint16_t)st.raw_stream.port;
    rs_cfg.queue_kb = (uint16_t)st.raw_stream.queue_kb;
    rs_cfg.reconnect_ms = (uint32_t)st.raw_stream.reconnect_ms;
    rs_cfg.connect_timeout_ms = (uint32_t)st.raw_stream.connect_timeout_ms;
    rs_cfg.serial_mirror = st.raw_stream.serial_mirror;
    raw_frame_streamer_init(rs_cfg);

    // 5. WiFi (non-blocking start — actual connection happens in loop)
    g_wifi.setupWiFi(st.wifi.ssid, st.wifi.password, st.network.hostname);

    // Wait up to 15s for initial connection before starting MQTT/Web
    uint32_t t = millis();
    while (!g_wifi.isConnected() && millis()-t < 15000) {
        delay(200); UnifiedLogger::raw(".");
    }
    UnifiedLogger::raw("\n");
    bool connected = g_wifi.isConnected();
    // LED: first WiFi verdict — YELLOW = WiFi OK (MQTT pending), WHITE = no connection yet
    g_led.setStatus(connected ? STATUS_YELLOW : STATUS_WHITE);

    // 6. mDNS
    if (connected) mdns_start();

    // 7. MQTT
    String lwt = st.mqtt.base_topic + "/status";
    g_mqtt.setup(st.mqtt.server, st.mqtt.port,
                 st.mqtt.client_id, st.mqtt.user, st.mqtt.password, lwt);
    mqtt_publisher_init(&g_mqtt, &g_config.getSettings());
    if (connected) g_mqtt.reconnect();

    // 8. Web UI (serves even without WiFi — mDNS won't work but IP will if connected)
    web_ui_init(&g_config, &g_mqtt, &g_ota, !connected);

    // 9. Sniffer task on core 1, priority 5
    xTaskCreatePinnedToCore(sniffer_task, "sniffer", 4096, nullptr, 5,
                             &s_sniffer_task, 1);

    UnifiedLogger::info("[MAIN] ready — IP=%s\n",
                        connected ? WiFi.localIP().toString().c_str() : "none");
}

void loop() {
    // WiFi watchdog
    g_wifi.checkWiFiConnection();

    // OTA (only active while armed via web UI)
    g_ota.loop();

    // MQTT (reconnect throttled internally + keepalive loop)
    mqtt_loop();
    mqtt_tick();  // tiered flush: HIGH every 10s, MEDIUM 30s, LOW 60s (retained)

    // Web and decoder
    web_ui_loop();
    huawei_decoder_expire_pending(2000);



    // LED diagnostic — update every 500 ms based on current operational state
    static uint32_t last_led_ms = 0;
    if (millis() - last_led_ms >= 500) {
        last_led_ms = millis();
        const uint32_t fh = esp_get_free_heap_size();
        if      (fh < psram_limits::kHeapCriticalThresholdBytes) g_led.setStatus(STATUS_MAGENTA); // heap critical
        else if (!g_wifi.isConnected())                          g_led.setStatus(STATUS_RED);     // WiFi lost
        else if (!mqtt_connected())                              g_led.setStatus(STATUS_YELLOW);  // MQTT down
        else if (g_frames_decoded == 0)                          g_led.setStatus(STATUS_CYAN);    // awaiting first frame
        else                                                     g_led.setStatus(STATUS_GREEN);   // all operational
    }

    // Heartbeat every 60 s — heap, MCU temp, RSSI
    static uint32_t last_hb = 0;
    if (millis() - last_hb > 60000) {
        last_hb = millis();
        const uint32_t free_heap = esp_get_free_heap_size();
        g_chiptemp.updateTemperature();
        const float mcu_t = g_chiptemp.getTemperature();
        if (!isnan(mcu_t)) {
            UnifiedLogger::info("[MAIN] heap=%u | mqtt=%s | frames=%u | rssi=%d dBm | mcu=%.1f°C\n",
                                free_heap, mqtt_connected() ? "ok" : "offline",
                                (unsigned)g_frames_decoded, WiFi.RSSI(), mcu_t);
        } else {
            UnifiedLogger::info("[MAIN] heap=%u | mqtt=%s | frames=%u | rssi=%d dBm\n",
                                free_heap, mqtt_connected() ? "ok" : "offline",
                                (unsigned)g_frames_decoded, WiFi.RSSI());
        }
        if (free_heap < psram_limits::kHeapWarningThresholdBytes) {
            UnifiedLogger::warning("[MAIN] Low heap: %u bytes free (threshold: %u)\n",
                                   free_heap, (unsigned)psram_limits::kHeapWarningThresholdBytes);
        }
    }

    delay(10);
}
