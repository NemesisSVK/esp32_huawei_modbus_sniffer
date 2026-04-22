#ifndef RGB_LED_MANAGER_H
#define RGB_LED_MANAGER_H

#include <Adafruit_NeoPixel.h>

/**
 * LedStatus — sniffer diagnostic states, shown on the built-in NeoPixel (GPIO 48).
 *
 * Color map:
 *   BLUE    — Booting / initialising
 *   WHITE   — WiFi connecting (no IP yet)
 *   RED     — WiFi disconnected / lost
 *   YELLOW  — WiFi OK, MQTT disconnected
 *   CYAN    — WiFi+MQTT OK, but no Modbus frames received yet
 *   GREEN   — Fully operational (WiFi+MQTT+frames)
 *   MAGENTA — Heap critical (< kHeapCriticalThresholdBytes)
 *   OFF     — LED explicitly disabled
 */
enum LedStatus {
    STATUS_OFF,
    STATUS_RED,
    STATUS_YELLOW,
    STATUS_GREEN,
    STATUS_MAGENTA,
    STATUS_BLUE,
    STATUS_CYAN,
    STATUS_WHITE
};

class RGBLedManager {
public:
    RGBLedManager(const RGBLedManager&)            = delete;
    RGBLedManager& operator=(const RGBLedManager&) = delete;

    explicit RGBLedManager(int pin = 48, int numPixels = 1);
    ~RGBLedManager();

    void begin();
    void setStatus(LedStatus status);

private:
    Adafruit_NeoPixel* pixel;
    LedStatus          currentStatus;
    bool               hasShown;
    void               setColor(uint32_t color);
};

#endif // RGB_LED_MANAGER_H
