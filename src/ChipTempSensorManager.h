#ifndef CHIP_TEMP_SENSOR_MANAGER_H
#define CHIP_TEMP_SENSOR_MANAGER_H

#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

/**
 * ChipTempSensorManager — reads the ESP32-S3 internal temperature sensor
 * via Arduino's built-in temperatureRead() function.
 *
 * Accuracy: ±5°C typical. Useful as a thermal indicator in hot environments
 * (e.g. inverter cabinet). Reading is updated by calling updateTemperature(),
 * which should be called periodically (e.g. every 60 s in the heartbeat).
 */
class ChipTempSensorManager {
public:
    ChipTempSensorManager();
    ~ChipTempSensorManager();

    bool  begin();
    bool  updateTemperature();
    float getTemperature();
    bool  isInitialized();
    bool  isSensorOperational();

private:
    float temperature;
    bool  initialized;

#if defined(ARDUINO_ARCH_ESP32)
    mutable SemaphoreHandle_t stateMutex = nullptr;
#endif

    void lockState() const;
    void unlockState() const;

    class StateLockGuard {
    public:
        explicit StateLockGuard(const ChipTempSensorManager& owner) : owner_(owner) {
            owner_.lockState();
        }
        ~StateLockGuard() { owner_.unlockState(); }
    private:
        const ChipTempSensorManager& owner_;
    };
};

#endif // CHIP_TEMP_SENSOR_MANAGER_H
