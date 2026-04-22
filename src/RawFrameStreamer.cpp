#include "RawFrameStreamer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include "UnifiedLogger.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <esp_heap_caps.h>
#endif

namespace {
static constexpr uint32_t STREAM_MAGIC = 0x52465331u; // "RFS1"
static constexpr uint8_t STREAM_VERSION = 1;
static constexpr uint16_t MIN_QUEUE_KB = 32;
static constexpr uint16_t MAX_QUEUE_KB = 2048;
static constexpr uint16_t MIN_SLOTS = 8;

struct StreamSlot {
    uint32_t seq;
    uint32_t ts_ms;
    uint16_t raw_len;
    uint8_t frame_type;
    uint8_t slave;
    uint8_t fc;
    uint8_t sub;
    uint8_t raw[MODBUS_RTU_MAX_FRAME];
};

static StreamSlot* s_slots = nullptr;
static uint16_t s_capacity = 0;
static uint16_t s_head = 0;
static uint16_t s_tail = 0;
static uint16_t s_count = 0;

static RawFrameStreamConfig s_cfg = {};
static bool s_connected = false;
static uint32_t s_seq = 1;
static uint32_t s_enqueued_frames = 0;
static uint32_t s_sent_frames = 0;
static uint32_t s_dropped_frames = 0;
static uint32_t s_failed_connects = 0;
static uint32_t s_reconnect_count = 0;
static bool s_ever_connected = false;

static TaskHandle_t s_task_handle = nullptr;

#if defined(ARDUINO_ARCH_ESP32)
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#define RAW_LOCK()   portENTER_CRITICAL(&s_mux)
#define RAW_UNLOCK() portEXIT_CRITICAL(&s_mux)
#else
#define RAW_LOCK()
#define RAW_UNLOCK()
#endif

static inline uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static uint8_t map_frame_type(ModbusFrameType t) {
    switch (t) {
        case FRAME_REQUEST:   return 0;
        case FRAME_RESPONSE:  return 1;
        case FRAME_EXCEPTION: return 2;
        case FRAME_UNKNOWN:   return 3;
        default:              return 0xFF;
    }
}

static bool alloc_slots(uint16_t queue_kb) {
    queue_kb = clamp_u16(queue_kb, MIN_QUEUE_KB, MAX_QUEUE_KB);
    const size_t queue_bytes = (size_t)queue_kb * 1024u;
    const size_t slot_sz = sizeof(StreamSlot);
    uint16_t slot_count = (uint16_t)(queue_bytes / slot_sz);
    if (slot_count < MIN_SLOTS) slot_count = MIN_SLOTS;

#if defined(ARDUINO_ARCH_ESP32)
    s_slots = static_cast<StreamSlot*>(heap_caps_malloc((size_t)slot_count * slot_sz,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_slots) {
        s_slots = static_cast<StreamSlot*>(heap_caps_malloc((size_t)slot_count * slot_sz,
                                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
#else
    s_slots = static_cast<StreamSlot*>(malloc((size_t)slot_count * slot_sz));
#endif
    if (!s_slots) return false;
    memset(s_slots, 0, (size_t)slot_count * slot_sz);
    s_capacity = slot_count;
    return true;
}

static bool pop_slot(StreamSlot* out) {
    if (!out) return false;
    RAW_LOCK();
    if (!s_slots || s_count == 0) {
        RAW_UNLOCK();
        return false;
    }
    *out = s_slots[s_tail];
    s_tail = (uint16_t)((s_tail + 1u) % s_capacity);
    s_count--;
    RAW_UNLOCK();
    return true;
}

static void push_slot(const ModbusFrame* f) {
    RAW_LOCK();
    if (!s_slots || s_capacity == 0) {
        RAW_UNLOCK();
        return;
    }

    if (s_count == s_capacity) {
        s_tail = (uint16_t)((s_tail + 1u) % s_capacity);
        s_count--;
        s_dropped_frames++;
    }

    StreamSlot& slot = s_slots[s_head];
    slot.seq = s_seq++;
    slot.ts_ms = (uint32_t)millis();
    slot.raw_len = f->raw_len;
    slot.frame_type = map_frame_type(f->type);
    slot.slave = f->slave_addr;
    slot.fc = f->function_code;
    slot.sub = (f->raw_len > 2) ? f->raw[2] : 0;
    memcpy(slot.raw, f->raw, f->raw_len);

    s_head = (uint16_t)((s_head + 1u) % s_capacity);
    s_count++;
    s_enqueued_frames++;
    RAW_UNLOCK();
}

static void set_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void set_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

// Wire format:
//   0..3  : "RFS1"
//   4     : version (1)
//   5     : frame_type
//   6     : slave
//   7     : fc
//   8     : sub
//   9     : reserved
//   10..13: seq (be32)
//   14..17: ts_ms (be32)
//   18..19: raw_len (be16)
//   20..  : raw bytes
static bool send_slot(WiFiClient& client, const StreamSlot& slot) {
    uint8_t hdr[20];
    set_be32(&hdr[0], STREAM_MAGIC);
    hdr[4] = STREAM_VERSION;
    hdr[5] = slot.frame_type;
    hdr[6] = slot.slave;
    hdr[7] = slot.fc;
    hdr[8] = slot.sub;
    hdr[9] = 0;
    set_be32(&hdr[10], slot.seq);
    set_be32(&hdr[14], slot.ts_ms);
    set_be16(&hdr[18], slot.raw_len);

    const size_t hwr = client.write(hdr, sizeof(hdr));
    if (hwr != sizeof(hdr)) return false;
    if (slot.raw_len == 0) return true;
    const size_t dwr = client.write(slot.raw, slot.raw_len);
    return dwr == slot.raw_len;
}

static void mark_connected(bool connected) {
    RAW_LOCK();
    s_connected = connected;
    RAW_UNLOCK();
}

static bool is_enabled() {
    RAW_LOCK();
    const bool en = s_cfg.enabled && (s_slots != nullptr) && s_capacity > 0;
    RAW_UNLOCK();
    return en;
}

static void stream_task(void* /*pv*/) {
    WiFiClient client;
    uint32_t last_attempt_ms = 0;
    bool was_connected = false;

    for (;;) {
        if (!is_enabled()) {
            if (client.connected()) client.stop();
            mark_connected(false);
            was_connected = false;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (WiFi.status() != WL_CONNECTED) {
            if (client.connected()) client.stop();
            mark_connected(false);
            was_connected = false;
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        if (!client.connected()) {
            mark_connected(false);
            was_connected = false;

            const uint32_t now = (uint32_t)millis();
            uint32_t reconnect_ms;
            RAW_LOCK();
            reconnect_ms = s_cfg.reconnect_ms;
            RAW_UNLOCK();
            if ((uint32_t)(now - last_attempt_ms) < reconnect_ms) {
                vTaskDelay(pdMS_TO_TICKS(25));
                continue;
            }
            last_attempt_ms = now;

            char host[96] = {0};
            uint16_t port = 0;
            uint32_t connect_timeout_ms = 1500;
            RAW_LOCK();
            strncpy(host, s_cfg.host ? s_cfg.host : "", sizeof(host) - 1);
            port = s_cfg.port;
            connect_timeout_ms = s_cfg.connect_timeout_ms;
            RAW_UNLOCK();

            bool ok = false;
            if (host[0] != '\0' && port != 0) {
                ok = client.connect(host, port, (int32_t)connect_timeout_ms);
            }
            if (!ok) {
                RAW_LOCK();
                s_failed_connects++;
                RAW_UNLOCK();
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            client.setNoDelay(true);
            mark_connected(true);
            if (s_ever_connected) {
                RAW_LOCK();
                s_reconnect_count++;
                RAW_UNLOCK();
            }
            s_ever_connected = true;
            UnifiedLogger::info("[RAWSTRM] connected to %s:%u\n", host, (unsigned)port);
        }

        if (!was_connected) {
            was_connected = true;
            mark_connected(true);
        }

        StreamSlot slot;
        if (!pop_slot(&slot)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (!send_slot(client, slot)) {
            client.stop();
            mark_connected(false);
            was_connected = false;
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        RAW_LOCK();
        s_sent_frames++;
        RAW_UNLOCK();
    }
}

} // namespace

void raw_frame_streamer_init(const RawFrameStreamConfig& cfg) {
    RAW_LOCK();
    s_cfg = cfg;
    s_cfg.queue_kb = clamp_u16(s_cfg.queue_kb, MIN_QUEUE_KB, MAX_QUEUE_KB);
    if (s_cfg.reconnect_ms < 100) s_cfg.reconnect_ms = 100;
    if (s_cfg.connect_timeout_ms < 100) s_cfg.connect_timeout_ms = 100;
    if (s_cfg.connect_timeout_ms > 30000) s_cfg.connect_timeout_ms = 30000;
    RAW_UNLOCK();

    if (!cfg.enabled) {
        UnifiedLogger::info("[RAWSTRM] disabled\n");
        return;
    }

    if (!cfg.host || cfg.host[0] == '\0' || cfg.port == 0) {
        UnifiedLogger::warning("[RAWSTRM] disabled: host/port not configured\n");
        RAW_LOCK();
        s_cfg.enabled = false;
        RAW_UNLOCK();
        return;
    }

    if (!alloc_slots(cfg.queue_kb)) {
        UnifiedLogger::error("[RAWSTRM] queue alloc failed (%u KB)\n", (unsigned)cfg.queue_kb);
        RAW_LOCK();
        s_cfg.enabled = false;
        RAW_UNLOCK();
        return;
    }

    if (!s_task_handle) {
        xTaskCreatePinnedToCore(stream_task, "raw_stream", 6144, nullptr, 2, &s_task_handle, 0);
    }
    UnifiedLogger::info("[RAWSTRM] enabled host=%s:%u queue=%u slots mirror=%s\n",
                        cfg.host, (unsigned)cfg.port,
                        (unsigned)s_capacity,
                        cfg.serial_mirror ? "yes" : "no");
}

void raw_frame_streamer_enqueue(const ModbusFrame* frame) {
    if (!raw_frame_streamer_is_enabled()) return;
    if (!frame || !frame->raw || frame->raw_len == 0) return;
    if (frame->raw_len > MODBUS_RTU_MAX_FRAME) return;
    push_slot(frame);
}

bool raw_frame_streamer_is_enabled() {
    RAW_LOCK();
    const bool en = s_cfg.enabled && (s_slots != nullptr) && s_capacity > 0;
    RAW_UNLOCK();
    return en;
}

bool raw_frame_streamer_serial_mirror() {
    RAW_LOCK();
    const bool mirror = s_cfg.serial_mirror;
    RAW_UNLOCK();
    return mirror;
}

void raw_frame_streamer_get_stats(RawFrameStreamStats* out) {
    if (!out) return;
    RAW_LOCK();
    out->enabled = s_cfg.enabled;
    out->connected = s_connected;
    out->queued_frames = s_count;
    out->queue_capacity = s_capacity;
    out->enqueued_frames = s_enqueued_frames;
    out->sent_frames = s_sent_frames;
    out->dropped_frames = s_dropped_frames;
    out->failed_connects = s_failed_connects;
    out->reconnect_count = s_reconnect_count;
    RAW_UNLOCK();
}
