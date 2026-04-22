#pragma once

// ============================================================
// HARDWARE CONFIGURATION — compile-time constants only
//
// Runtime configuration (WiFi, MQTT, RS-485 baud, GPIO pins,
// security, device info) is managed via data/config.json and
// the web UI — it does NOT belong here.
//
// Only things that cannot change without a firmware rebuild
// belong in this file.
// ============================================================

// ---- UART Peripheral ----
// Which ESP32-S3 UART port to use for RS-485 (UART_NUM_1 or UART_NUM_2).
// GPIO pin assignments are runtime-configurable via config.json / web UI.
#define RS485_UART_NUM    UART_NUM_1

// ---- UART Driver Buffer ----
// Increase if you see overrun errors at high baud rates.
#define UART_RX_BUF_SIZE  1024

// ---- Modbus frame size limit ----
// Standard RTU max is 256 bytes (function code + 253 data + 3 overhead).
#define MODBUS_MAX_FRAME_LEN  256

// ---- Logging ----
// Logging is controlled at RUNTIME via UnifiedLogger::setEnabled(true/false).
// Call UnifiedLogger::begin(false) in setup() to boot silently.
// The old compile-time #define DEBUG_LEVEL has been removed — all Serial output
// is now routed through UnifiedLogger and can be toggled without a rebuild.
// #define DEBUG_LEVEL  2   ← replaced by runtime UnifiedLogger

// ---- Built-in NeoPixel LED ----
// ESP32-S3 DevKit (N16R8V) has a WS2812 RGB LED on GPIO 48.
// Used by RGBLedManager for diagnostic status indication.
#define LED_BUILTIN_PIN  48
