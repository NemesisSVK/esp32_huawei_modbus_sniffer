#pragma once
#include <Arduino.h>
#include "AsyncMqttTransport.h"

/**
 * MQTTManager — async MQTT connection management for the Modbus Sniffer.
 *
 * Uses AsyncMqttTransport (AsyncMqttClient-esphome) which is:
 *   - Fully event-driven — no client.loop() TCP blocking call needed
 *   - FreeRTOS-safe via portMUX — safe alongside the sniffer task on core 1
 *   - QoS 0/1/2 capable
 *
 * Connection pattern:
 *   - setup()    : configure broker params + keepalive
 *   - reconnect(): non-blocking, 10 s throttle, called from loop()
 *   - loop()     : polls transport events, drives connection-state tracking
 *   - publish()  : direct async publish, returns false if disconnected
 *
 * LWT: <base_topic>/status → "offline" (retained, QoS 1)
 * Birth: same topic → "online"  on every connect
 */
class MQTTManager {
public:
    MQTTManager();
    ~MQTTManager() = default;

    /** Configure broker, credentials and LWT.  Call once before first loop(). */
    void setup(const String& server, int port,
               const String& clientId,
               const String& user     = "",
               const String& password = "",
               const String& lwtTopic = "");

    /** Must be called from loop() — drives transport events + connection-state tracking. */
    void loop();

    /** Non-blocking reconnect attempt (throttled to RECONNECT_THROTTLE_MS). */
    void reconnect();

    bool isConnected();

    /** Async publish.  Returns false (and logs) if not connected or transport rejects. */
    bool publish(const String& topic, const String& payload,
                 bool retained = false, uint8_t qos = 0);

    // Connection duration helpers (used by web UI status)
    unsigned long getDisconnectionDuration() const;
    unsigned long getConnectionDuration()    const;

    /** Human-readable reason for the current disconnect state. */
    String getStateReason();

private:
    AsyncMqttTransport asyncTransport;

    String mqtt_server;
    int    mqtt_port     = 1883;
    String client_id;
    String mqtt_user;
    String mqtt_password;
    String lwt_topic;

    // Reconnect throttle
    unsigned long lastReconnectAttemptMs   = 0;
    bool          connectionAttemptPending = false;
    unsigned long connectAttemptStartedMs  = 0;
    static const unsigned long RECONNECT_THROTTLE_MS  = 10000UL;
    static const unsigned long CONNECT_GUARD_MS       = 10000UL;

    // Connection state tracking
    bool          lastConnectionState = false;
    unsigned long disconnectionTime   = 0;
    unsigned long connectionTime      = 0;

    void trackConnectionState(bool connected);
    void processTransportEvents();
};
