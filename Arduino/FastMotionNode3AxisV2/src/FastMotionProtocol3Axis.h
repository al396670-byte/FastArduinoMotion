#pragma once

#include <Arduino.h>

namespace fastmotion3 {
namespace protocol {

// Wire protocol remains version 2. The Arduino library patch version is 2.0.1.
static const uint8_t VERSION = 2;
static const uint8_t COMMAND_TYPE = 1;
static const uint8_t STATUS_TYPE = 2;

static const uint8_t COMMAND_SYNC_0 = 0xA5;
static const uint8_t COMMAND_SYNC_1 = 0x5A;
static const uint8_t STATUS_SYNC_0 = 0x5A;
static const uint8_t STATUS_SYNC_1 = 0xA5;

static const uint8_t COMMAND_SIZE = 28;
static const uint8_t STATUS_SIZE = 48;
static const uint8_t AXIS_COUNT = 3;

// Axis selection masks use bits 0, 1 and 2.
static const uint16_t AXIS_1_MASK = 0x0001;
static const uint16_t AXIS_2_MASK = 0x0002;
static const uint16_t AXIS_3_MASK = 0x0004;
static const uint16_t VALID_AXIS_MASK = 0x0007;

// Node statusword.
static const uint16_t NODE_STATUS_LINK_ACTIVE = 0x0001;
static const uint16_t NODE_STATUS_WATCHDOG_OK = 0x0002;
static const uint16_t NODE_STATUS_PROTOCOL_READY = 0x0004;
static const uint16_t NODE_STATUS_ANY_AXIS_RUNNING = 0x0008;
static const uint16_t NODE_STATUS_POSITION_ESTIMATED = 0x0010;

// Per-axis statusword.
static const uint16_t AXIS_STATUS_ENABLED = 0x0001;
static const uint16_t AXIS_STATUS_RUNNING = 0x0002;
static const uint16_t AXIS_STATUS_POSITION_ESTIMATED = 0x0004;
static const uint16_t AXIS_STATUS_VELOCITY_CLAMPED = 0x0008;
static const uint16_t AXIS_STATUS_QUICK_STOP = 0x0010;
static const uint16_t AXIS_STATUS_FAULT = 0x0020;

// Diagnostic/error codes. Watchdog timeout is a safe-state event, not a latched
// motor fault. Version 2.0.1 does not leave stale watchdog-fault frames queued
// while the PLC is stopped.
enum ErrorCode : uint16_t {
    ERROR_NONE = 0,
    ERROR_BAD_CRC = 1,
    ERROR_BAD_VERSION = 2,
    ERROR_BAD_TYPE = 3,
    ERROR_WATCHDOG = 4,
    ERROR_VELOCITY_CLAMP = 5,
    ERROR_BAD_MASK = 6
};

// PLC -> Arduino, 28 bytes, little-endian:
//  0..1   A5 5A
//  2      protocol version = 2
//  3      frame type = 1
//  4..5   sequence
//  6..7   watchdog [ms]
//  8..9   enable mask
// 10..11  quick-stop mask
// 12..13  reset-position mask
// 14..17  velocity axis 1 [steps/s], int32
// 18..21  velocity axis 2 [steps/s], int32
// 22..25  velocity axis 3 [steps/s], int32
// 26..27  CRC16/Modbus over bytes 0..25

// Arduino -> PLC, 48 bytes, little-endian:
//  0..1   5A A5
//  2      protocol version = 2
//  3      frame type = 2
//  4..5   acknowledged sequence
//  6..7   node statusword
//  8..9   node error
// 10..11  ready mask
// 12..13  error mask
// 14..17  position axis 1 [steps], int32
// 18..21  position axis 2 [steps], int32
// 22..25  position axis 3 [steps], int32
// 26..29  applied velocity axis 1 [steps/s], int32
// 30..33  applied velocity axis 2 [steps/s], int32
// 34..37  applied velocity axis 3 [steps/s], int32
// 38..39  axis 1 statusword
// 40..41  axis 2 statusword
// 42..43  axis 3 statusword
// 44..45  age of last valid command [ms], saturated uint16
// 46..47  CRC16/Modbus over bytes 0..45

inline uint16_t readU16LE(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

inline int32_t readI32LE(const uint8_t* data) {
    const uint32_t raw = static_cast<uint32_t>(data[0]) |
                         (static_cast<uint32_t>(data[1]) << 8) |
                         (static_cast<uint32_t>(data[2]) << 16) |
                         (static_cast<uint32_t>(data[3]) << 24);
    return static_cast<int32_t>(raw);
}

inline void writeU16LE(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFFU);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

inline void writeI32LE(uint8_t* data, int32_t value) {
    const uint32_t raw = static_cast<uint32_t>(value);
    data[0] = static_cast<uint8_t>(raw & 0xFFUL);
    data[1] = static_cast<uint8_t>((raw >> 8) & 0xFFUL);
    data[2] = static_cast<uint8_t>((raw >> 16) & 0xFFUL);
    data[3] = static_cast<uint8_t>((raw >> 24) & 0xFFUL);
}

inline uint16_t crc16Modbus(const uint8_t* data, uint8_t length) {
    uint16_t crc = 0xFFFFU;
    for (uint8_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001U);
            } else {
                crc = static_cast<uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

}  // namespace protocol
}  // namespace fastmotion3
