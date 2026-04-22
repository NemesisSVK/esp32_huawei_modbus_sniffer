#include "AsyncMqttTransport.h"

namespace {
uint8_t clampMqttQos(uint8_t qos) {
    return qos > 2 ? 2 : qos;
}
}

AsyncMqttTransport::AsyncMqttTransport()
    : callbacksInstalled(false),
      connected(false),
      lastState(-1),
      pendingConnectedEvents(0),
      pendingDisconnectedEvents(0),
      lastDisconnectState(-1)
#if defined(ARDUINO_ARCH_ESP32)
      ,
      stateLock(portMUX_INITIALIZER_UNLOCKED)
#endif
{
    installCallbacksOnce();
}

void AsyncMqttTransport::setServer(const String& server, int port) {
    connectServerHost = server;
    client.setServer(connectServerHost.c_str(), static_cast<uint16_t>(port));
}

void AsyncMqttTransport::setKeepAlive(uint16_t keepAliveSeconds) {
    client.setKeepAlive(keepAliveSeconds);
}

void AsyncMqttTransport::setSocketTimeout(uint16_t timeoutSeconds) {
    // AsyncMqttClient does not expose a socket-timeout setter; keep for interface parity.
    (void)timeoutSeconds;
}

void AsyncMqttTransport::setBufferSize(uint16_t bufferSizeBytes) {
    // AsyncMqttClient manages internal buffering; no explicit buffer-size API.
    (void)bufferSizeBytes;
}

bool AsyncMqttTransport::connect(const String& clientId,
                                 const String& user,
                                 const String& password,
                                 const String& willTopic,
                                 uint8_t willQos,
                                 bool willRetain,
                                 const String& willMessage) {
    installCallbacksOnce();

    // AsyncMqttClient keeps pointers for these fields, so persist backing storage.
    connectClientId = clientId;
    connectUser = user;
    connectPassword = password;
    connectWillTopic = willTopic;
    connectWillMessage = willMessage;

    client.setClientId(connectClientId.c_str());
    if (connectUser.length() > 0) {
        client.setCredentials(connectUser.c_str(), connectPassword.c_str());
    } else {
        client.setCredentials(nullptr, nullptr);
    }

    const uint8_t normalizedWillQos = clampMqttQos(willQos);
    client.setWill(connectWillTopic.c_str(), normalizedWillQos, willRetain, connectWillMessage.c_str());

    // Asynchronous connect: returns immediately. Connection result is delivered via callbacks.
    if (isConnected()) {
        return true;
    }
    client.connect();
    return true;
}

bool AsyncMqttTransport::isConnected() {
    lockState();
    const bool currentConnected = connected;
    unlockState();
    return currentConnected;
}

void AsyncMqttTransport::loop() {
    // Event-driven client; no polling loop required.
}

int AsyncMqttTransport::state() {
    lockState();
    const int currentState = lastState;
    unlockState();
    return currentState;
}

bool AsyncMqttTransport::pollConnectionEvents(ConnectionEvents& outEvents) {
    outEvents = ConnectionEvents{};

    lockState();
    if (pendingConnectedEvents > 0) {
        outEvents.connected = true;
        pendingConnectedEvents = 0;
    }
    if (pendingDisconnectedEvents > 0) {
        outEvents.disconnected = true;
        outEvents.disconnectState = lastDisconnectState;
        pendingDisconnectedEvents = 0;
    }
    unlockState();

    return outEvents.connected || outEvents.disconnected;
}

bool AsyncMqttTransport::publish(const String& topic,
                                 const String& payload,
                                 bool retained,
                                 uint8_t qos) {
    if (!isConnected()) {
        return false;
    }
    const uint8_t normalizedQos = clampMqttQos(qos);
    const uint16_t packetId = client.publish(topic.c_str(), normalizedQos, retained, payload.c_str());
    return packetId != 0;
}

void AsyncMqttTransport::installCallbacksOnce() {
    if (callbacksInstalled) {
        return;
    }

    client.onConnect([this](bool sessionPresent) {
        (void)sessionPresent;
        lockState();
        connected = true;
        lastState = 0;
        pendingConnectedEvents++;
        unlockState();
    });

    client.onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
        lockState();
        connected = false;
        lastState = static_cast<int>(reason);
        lastDisconnectState = lastState;
        pendingDisconnectedEvents++;
        unlockState();
    });

    callbacksInstalled = true;
}

void AsyncMqttTransport::lockState() {
#if defined(ARDUINO_ARCH_ESP32)
    portENTER_CRITICAL(&stateLock);
#endif
}

void AsyncMqttTransport::unlockState() {
#if defined(ARDUINO_ARCH_ESP32)
    portEXIT_CRITICAL(&stateLock);
#endif
}
