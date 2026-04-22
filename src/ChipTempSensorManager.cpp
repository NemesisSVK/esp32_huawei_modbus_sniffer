#include "ChipTempSensorManager.h"
#include "UnifiedLogger.h"

ChipTempSensorManager::ChipTempSensorManager()
    : temperature(NAN), initialized(false)
{
#if defined(ARDUINO_ARCH_ESP32)
    stateMutex = xSemaphoreCreateRecursiveMutex();
#endif
}

ChipTempSensorManager::~ChipTempSensorManager() {
#if defined(ARDUINO_ARCH_ESP32)
    if (stateMutex) {
        vSemaphoreDelete(stateMutex);
        stateMutex = nullptr;
    }
#endif
}

void ChipTempSensorManager::lockState() const {
#if defined(ARDUINO_ARCH_ESP32)
    if (stateMutex) xSemaphoreTakeRecursive(stateMutex, portMAX_DELAY);
#endif
}

void ChipTempSensorManager::unlockState() const {
#if defined(ARDUINO_ARCH_ESP32)
    if (stateMutex) xSemaphoreGiveRecursive(stateMutex);
#endif
}

bool ChipTempSensorManager::begin() {
    StateLockGuard guard(*this);
    initialized = true;
    UnifiedLogger::info("[CHIPTEMP] Internal temperature sensor ready\n");
    return true;
}

bool ChipTempSensorManager::updateTemperature() {
    StateLockGuard guard(*this);
    if (!initialized) return false;

    float t = temperatureRead();   // Arduino built-in
    if (!isnan(t) && t > -50.0f && t < 150.0f) {
        temperature = roundf(t * 10.0f) / 10.0f;
        return true;
    }
    temperature = NAN;
    return false;
}

float ChipTempSensorManager::getTemperature() {
    StateLockGuard guard(*this);
    return temperature;
}

bool ChipTempSensorManager::isInitialized() {
    StateLockGuard guard(*this);
    return initialized;
}

bool ChipTempSensorManager::isSensorOperational() {
    StateLockGuard guard(*this);
    return !isnan(temperature);
}
