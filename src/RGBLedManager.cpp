#include "RGBLedManager.h"

RGBLedManager::RGBLedManager(int pin, int numPixels)
    : currentStatus(STATUS_WHITE), hasShown(false)
{
    pixel = new Adafruit_NeoPixel(numPixels, pin, NEO_GRB + NEO_KHZ800);
}

RGBLedManager::~RGBLedManager() {
    if (pixel) { delete pixel; }
}

void RGBLedManager::begin() {
    pixel->begin();
    pixel->setBrightness(80);       // 80/255 — visible but not blinding
    hasShown = false;
    setStatus(STATUS_BLUE);         // Boot indicator
}

void RGBLedManager::setColor(uint32_t color) {
    pixel->setPixelColor(0, color);
    pixel->show();
}

void RGBLedManager::setStatus(LedStatus status) {
    if (hasShown && status == currentStatus) return;

    currentStatus = status;
    uint32_t color;
    switch (status) {
        case STATUS_OFF:     color = pixel->Color(0,   0,   0);   break;
        case STATUS_RED:     color = pixel->Color(255, 0,   0);   break;
        case STATUS_YELLOW:  color = pixel->Color(255, 200, 0);   break;
        case STATUS_GREEN:   color = pixel->Color(0,   255, 0);   break;
        case STATUS_MAGENTA: color = pixel->Color(255, 0,   255); break;
        case STATUS_BLUE:    color = pixel->Color(0,   0,   255); break;
        case STATUS_CYAN:    color = pixel->Color(0,   255, 255); break;
        case STATUS_WHITE:
        default:             color = pixel->Color(200, 200, 200); break;
    }
    setColor(color);
    hasShown = true;
}
