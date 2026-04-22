#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Modbus RTU frame detector + CRC validator + basic decoder
// ============================================================

// Minimum and maximum valid RTU frame lengths:
//   min: addr(1) + fc(1) + CRC(2) = 4 bytes
//   max: 256 bytes (spec limit)
#define MODBUS_RTU_MIN_FRAME  4
#define MODBUS_RTU_MAX_FRAME  256

// FC codes we care about
#define FC_READ_HOLDING_REGS  0x03
#define FC_READ_INPUT_REGS    0x04
#define FC_EXCEPTION_MASK     0x80   // FC | 0x80 = exception response

// ---- Frame classification ----
typedef enum {
    FRAME_UNKNOWN = 0,
    FRAME_REQUEST,           // master → slave (read request)
    FRAME_RESPONSE,          // slave → master (read response)
    FRAME_EXCEPTION,         // slave → master (error response)
} ModbusFrameType;

typedef struct {
    uint8_t  slave_addr;
    uint8_t  function_code;
    ModbusFrameType type;

    // For REQUEST frames:
    uint16_t req_start_addr;   // starting register address
    uint16_t req_reg_count;    // number of registers requested

    // For RESPONSE frames:
    uint8_t  rsp_byte_count;   // data byte count field
    const uint8_t* rsp_data;   // pointer into raw[] at the data start
    uint16_t rsp_reg_count;    // = rsp_byte_count / 2

    // Exception
    uint8_t  exception_code;

    // Raw frame
    const uint8_t* raw;
    uint16_t        raw_len;
} ModbusFrame;

// ---- Public API ----

/**
 * Compute Modbus CRC16 (poly 0xA001) over `len` bytes.
 * Returns true if the last 2 bytes of `data[0..len-1]` match the CRC
 * of `data[0..len-3]`.
 */
bool modbus_crc_valid(const uint8_t* data, uint16_t len);

/**
 * Compute CRC16 and return the value (for building frames, not used in
 * sniffer but useful for debug).
 */
uint16_t modbus_crc16(const uint8_t* data, uint16_t len);

/**
 * Try to parse `raw[0..len-1]` as a Modbus RTU frame.
 * Validates CRC first, then classifies the frame type.
 *
 * @param raw        Raw bytes (one complete inter-frame gap delimited chunk).
 * @param len        Number of bytes.
 * @param out        Output frame descriptor (populated on success).
 * @return true if CRC valid and frame parsed, false otherwise.
 */
bool modbus_parse_frame(const uint8_t* raw, uint16_t len, ModbusFrame* out);

/**
 * Decode one 16-bit register value from a response data buffer.
 * `reg_index` is 0-based (0 = first register in response).
 */
uint16_t modbus_get_u16(const ModbusFrame* frame, uint8_t reg_index);

/**
 * Decode one 32-bit (2-register) value from a response data buffer.
 * `reg_index` is the 0-based index of the HIGH word.
 * Big-endian: word[reg_index] = high, word[reg_index+1] = low.
 */
uint32_t modbus_get_u32(const ModbusFrame* frame, uint8_t reg_index);

int32_t  modbus_get_i32(const ModbusFrame* frame, uint8_t reg_index);
int16_t  modbus_get_i16(const ModbusFrame* frame, uint8_t reg_index);

/**
 * Pretty-print frame to Serial for debug.
 */
void modbus_print_frame(const ModbusFrame* frame);
