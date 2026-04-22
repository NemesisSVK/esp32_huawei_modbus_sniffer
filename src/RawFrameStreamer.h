#pragma once

#include <stdint.h>
#include "modbus_rtu.h"

// Dedicated raw-frame stream exporter for high-volume capture over Wi-Fi.
// Streams compact binary records to a collector app via TCP.

struct RawFrameStreamConfig {
    bool     enabled = false;
    const char* host = "";
    uint16_t port = 9900;
    uint16_t queue_kb = 256;
    uint32_t reconnect_ms = 1000;
    uint32_t connect_timeout_ms = 1500;
    bool     serial_mirror = false; // when false, captured frames go to stream only
};

struct RawFrameStreamStats {
    bool     enabled = false;
    bool     connected = false;
    uint16_t queued_frames = 0;
    uint16_t queue_capacity = 0;
    uint32_t enqueued_frames = 0;
    uint32_t sent_frames = 0;
    uint32_t dropped_frames = 0;
    uint32_t failed_connects = 0;
    uint32_t reconnect_count = 0;
};

void raw_frame_streamer_init(const RawFrameStreamConfig& cfg);
void raw_frame_streamer_enqueue(const ModbusFrame* frame);
bool raw_frame_streamer_is_enabled();
bool raw_frame_streamer_serial_mirror();
void raw_frame_streamer_get_stats(RawFrameStreamStats* out);
