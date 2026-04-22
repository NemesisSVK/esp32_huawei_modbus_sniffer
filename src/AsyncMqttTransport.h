#ifndef ASYNCMQTTTRANSPORT_H
#define ASYNCMQTTTRANSPORT_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

class AsyncMqttTransport {
public:
    struct ConnectionEvents {
        bool connected = false;
        bool disconnected = false;
        int disconnectState = 0;
    };

    AsyncMqttTransport();
    ~AsyncMqttTransport() = default;

    void setServer(const String& server, int port);
    void setKeepAlive(uint16_t keepAliveSeconds);
    void setSocketTimeout(uint16_t timeoutSeconds);
    void setBufferSize(uint16_t bufferSizeBytes);

    bool connect(const String& clientId,
                 const String& user,
                 const String& password,
                 const String& willTopic,
                 uint8_t willQos,
                 bool willRetain,
                 const String& willMessage);

    bool isConnected();
    void loop();
    int state();
    bool pollConnectionEvents(ConnectionEvents& outEvents);

    bool publish(const String& topic,
                 const String& payload,
                 bool retained,
                 uint8_t qos);

private:
    void lockState();
    void unlockState();
    void installCallbacksOnce();

    // AsyncMqttClient stores raw pointers for identity/credential/will fields.
    // Keep owning String storage alive across async runtime.
    String connectServerHost;
    String connectClientId;
    String connectUser;
    String connectPassword;
    String connectWillTopic;
    String connectWillMessage;

    AsyncMqttClient client;
    bool callbacksInstalled;
    bool connected;
    int lastState;
    uint32_t pendingConnectedEvents;
    uint32_t pendingDisconnectedEvents;
    int lastDisconnectState;
#if defined(ARDUINO_ARCH_ESP32)
    portMUX_TYPE stateLock;
#endif
};

#endif // ASYNCMQTTTRANSPORT_H
