#include "modbus_rtu.h"
#include <Arduino.h>
#include "config.h"
#include "UnifiedLogger.h"

// ============================================================
// CRC16 (CRC-MODBUS, polynomial 0xA001)
// ============================================================

uint16_t modbus_crc16(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}

bool modbus_crc_valid(const uint8_t* data, uint16_t len) {
    if (len < MODBUS_RTU_MIN_FRAME) return false;
    uint16_t computed = modbus_crc16(data, len - 2);
    // CRC is appended little-endian: low byte first
    uint16_t received = (uint16_t)(data[len - 2]) | ((uint16_t)(data[len - 1]) << 8);
    return computed == received;
}

// ============================================================
// Frame parser
// ============================================================

bool modbus_parse_frame(const uint8_t* raw, uint16_t len, ModbusFrame* out) {
    if (len < MODBUS_RTU_MIN_FRAME || len > MODBUS_RTU_MAX_FRAME) return false;
    if (!modbus_crc_valid(raw, len)) return false;

    out->raw        = raw;
    out->raw_len    = len;
    out->slave_addr = raw[0];
    out->function_code = raw[1];
    out->type       = FRAME_UNKNOWN;

    // Exception response: FC | 0x80
    if (out->function_code & FC_EXCEPTION_MASK) {
        out->type           = FRAME_EXCEPTION;
        out->function_code  = out->function_code & ~FC_EXCEPTION_MASK;
        out->exception_code = (len >= 5) ? raw[2] : 0;
        return true;
    }

    uint8_t fc = out->function_code;

    if (fc == FC_READ_HOLDING_REGS || fc == FC_READ_INPUT_REGS) {
        // REQUEST: addr(1) + fc(1) + start(2) + count(2) + crc(2) = 8 bytes
        if (len == 8) {
            out->type           = FRAME_REQUEST;
            out->req_start_addr = ((uint16_t)raw[2] << 8) | raw[3];
            out->req_reg_count  = ((uint16_t)raw[4] << 8) | raw[5];
            out->rsp_data       = nullptr;
            out->rsp_byte_count = 0;
            out->rsp_reg_count  = 0;
            return true;
        }

        // RESPONSE: addr(1) + fc(1) + byte_count(1) + data(n) + crc(2)
        // So: len = 5 + byte_count → byte_count = len - 5
        if (len >= 5) {
            uint8_t bc = raw[2];
            if ((uint16_t)(bc + 5) == len && (bc % 2 == 0)) {
                out->type           = FRAME_RESPONSE;
                out->rsp_byte_count = bc;
                out->rsp_data       = &raw[3];
                out->rsp_reg_count  = bc / 2;
                return true;
            }
        }
    }

    // Other function codes: parse generically as unknown but CRC-valid
    return true;
}

// ============================================================
// Register value accessors (big-endian word order)
// ============================================================

uint16_t modbus_get_u16(const ModbusFrame* f, uint8_t reg_index) {
    if (!f->rsp_data || reg_index >= f->rsp_reg_count) return 0;
    uint16_t offset = reg_index * 2;
    return ((uint16_t)f->rsp_data[offset] << 8) | f->rsp_data[offset + 1];
}

int16_t modbus_get_i16(const ModbusFrame* f, uint8_t reg_index) {
    return (int16_t)modbus_get_u16(f, reg_index);
}

uint32_t modbus_get_u32(const ModbusFrame* f, uint8_t reg_index) {
    if (!f->rsp_data || (reg_index + 1) >= f->rsp_reg_count) return 0;
    uint32_t hi = modbus_get_u16(f, reg_index);
    uint32_t lo = modbus_get_u16(f, reg_index + 1);
    return (hi << 16) | lo;
}

int32_t modbus_get_i32(const ModbusFrame* f, uint8_t reg_index) {
    return (int32_t)modbus_get_u32(f, reg_index);
}

// ============================================================
// Debug printer
// ============================================================

void modbus_print_frame(const ModbusFrame* f) {
    UnifiedLogger::verbose("[MODBUS] addr=0x%02X fc=0x%02X len=%u type=",
                           f->slave_addr, f->function_code, f->raw_len);

    switch (f->type) {
        case FRAME_REQUEST:
            UnifiedLogger::verbose("REQUEST  start=0x%04X (%u) count=%u\n",
                                   f->req_start_addr, f->req_start_addr, f->req_reg_count);
            break;
        case FRAME_RESPONSE:
            UnifiedLogger::verbose("RESPONSE regs=%u | data:", f->rsp_reg_count);
            for (uint8_t i = 0; i < f->rsp_reg_count; i++)
                UnifiedLogger::verbose(" %04X", modbus_get_u16(f, i));
            UnifiedLogger::verbose("\n");
            break;
        case FRAME_EXCEPTION:
            UnifiedLogger::verbose("EXCEPTION code=0x%02X\n", f->exception_code);
            break;
        default:
            UnifiedLogger::verbose("UNKNOWN\n");
    }
}
