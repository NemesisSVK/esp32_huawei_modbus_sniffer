#include "MQTTManager.h"
#include "UnifiedLogger.h"
#include <Arduino.h>

MQTTManager::MQTTManager()
    : asyncTransport() {
    asyncTransport.setKeepAlive(60);
    asyncTransport.setBufferSize(2048);
}

void MQTTManager::setup(const String& server, int port,
                        const String& clientId,
                        const String& user,
                        const String& password,
                        const String& lwtTopic) {
    mqtt_server   = server;
    mqtt_port     = port;
    client_id     = clientId;
    mqtt_user     = user;
    mqtt_password = password;
    lwt_topic     = lwtTopic;

    asyncTransport.setServer(mqtt_server, mqtt_port);
    asyncTransport.setKeepAlive(60);   // ~90s offline detection in HA
    asyncTransport.setSocketTimeout(60);

    UnifiedLogger::info("[MQTT] configured — broker=%s:%d client='%s'\n",
                        server.c_str(), port, clientId.c_str());
}

void MQTTManager::reconnect() {
    if (asyncTransport.isConnected()) {
        connectionAttemptPending = false;
        return;
    }

    const unsigned long now = millis();

    // Guard: if a connect attempt is already in flight, wait for the async result
    if (connectionAttemptPending) {
        if ((now - connectAttemptStartedMs) < CONNECT_GUARD_MS) return;
        connectionAttemptPending = false;  // timed out — allow retry
    }

    // Throttle rapid retries
    if (lastReconnectAttemptMs != 0 &&
        (now - lastReconnectAttemptMs) < RECONNECT_THROTTLE_MS) {
        return;
    }

    UnifiedLogger::info("[MQTT] connecting to %s:%d as '%s'...\n",
                        mqtt_server.c_str(), mqtt_port, client_id.c_str());

    asyncTransport.connect(client_id,
                           mqtt_user,
                           mqtt_password,
                           lwt_topic,
                           /*willQos*/   1,
                           /*willRetain*/true,
                           /*willMsg*/   "offline");

    connectionAttemptPending = true;
    connectAttemptStartedMs  = now;
    lastReconnectAttemptMs   = now;
}

bool MQTTManager::isConnected() {
    return asyncTransport.isConnected();
}

void MQTTManager::loop() {
    asyncTransport.loop();          // event-driven — returns immediately
    processTransportEvents();       // drain pending connect/disconnect events
    trackConnectionState(asyncTransport.isConnected());
}

bool MQTTManager::publish(const String& topic, const String& payload,
                           bool retained, uint8_t qos) {
    if (!asyncTransport.isConnected()) {
        // Not an error at boot — caller already guards on mqtt_connected()
        return false;
    }
    const bool ok = asyncTransport.publish(topic, payload, retained, qos);
    if (!ok) {
        UnifiedLogger::warning("[MQTT] publish failed: %s\n", topic.c_str());
    }
    return ok;
}

unsigned long MQTTManager::getDisconnectionDuration() const {
    return !lastConnectionState ? (millis() - disconnectionTime) : 0;
}

unsigned long MQTTManager::getConnectionDuration() const {
    return lastConnectionState ? (millis() - connectionTime) : 0;
}

String MQTTManager::getStateReason() {
    if (asyncTransport.isConnected()) return "connected";
    // Map AsyncMqttClientDisconnectReason codes to human-readable strings
    switch (asyncTransport.state()) {
        case  0: return "idle";
        case  1: return "unacceptable protocol version";
        case  2: return "client id rejected";
        case  3: return "server unavailable";
        case  4: return "bad credentials";
        case  5: return "not authorised";
        case 128: return "tcp disconnected";
        case 129: return "tls error";
        case 130: return "lost connection";
        case 131: return "malformed credentials";
        case 132: return "not authorised (async)";
        default:  return "unknown (" + String(asyncTransport.state()) + ")";
    }
}

// ============================================================
// Private
// ============================================================

void MQTTManager::processTransportEvents() {
    AsyncMqttTransport::ConnectionEvents ev;
    if (!asyncTransport.pollConnectionEvents(ev)) return;

    if (ev.connected) {
        UnifiedLogger::info("[MQTT] transport event: connected\n");
        connectionAttemptPending = false;
        // Birth message
        if (lwt_topic.length() > 0) {
            asyncTransport.publish(lwt_topic, "online", /*retained*/true, /*qos*/1);
        }
    }
    if (ev.disconnected) {
        connectionAttemptPending = false;
        UnifiedLogger::warning("[MQTT] transport event: disconnected (reason=%d)\n",
                               ev.disconnectState);
    }
}

void MQTTManager::trackConnectionState(bool connected) {
    if (connected == lastConnectionState) return;
    if (connected) {
        connectionTime = millis();
        UnifiedLogger::info("[MQTT] state → CONNECTED\n");
    } else {
        disconnectionTime = millis();
        UnifiedLogger::warning("[MQTT] state → DISCONNECTED\n");
    }
    lastConnectionState = connected;
}
