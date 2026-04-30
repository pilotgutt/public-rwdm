/**
 * @brief       tmp102.cpp
 * @details     TMP102 Digital Temperature Sensor driver implementation.
 *              Datasheet: https://www.ti.com/lit/ds/symlink/tmp102.pdf
 *
 * Temperature register (12-bit, two's complement):
 *   - Bits [15:4] hold the 12-bit signed temperature value
 *   - Resolution: 0.0625°C per LSB
 *   - Range: -40°C to +125°C
 *
 * Configuration register default (0x60A0):
 *   - SD=0 (continuous conversion), TM=0, POL=0, F=00
 *   - OS=0, R=11 (12-bit), CR=10 (4 Hz), AL=1, EM=0
 */
#include "mbed.h"
#include "tmp102.h"

TMP102::TMP102(I2C &i2c, uint8_t addr) : _i2c(i2c), _addr(addr) {}

TMP102::tmp102_status_t TMP102::init()
{
    // Write configuration register:
    // Byte 1: 0x60 -> SD=0 (continuous), TM=0, POL=0, F1:F0=00, R1:R0=11 (12-bit), OS=0
    // Byte 2: 0xA0 -> CR1:CR0=10 (4 Hz), AL=1, EM=0 (normal 12-bit mode)
    return write_register(REG_CONFIGURATION, 0x60A0);
}

TMP102::tmp102_status_t TMP102::read_temperature(float &temperature)
{
    uint16_t raw = 0;
    tmp102_status_t status = read_register(REG_TEMPERATURE, raw);
    if (status != TMP102_SUCCESS) {
        return TMP102_FAILURE;
    }

    // The 12-bit value is in the upper 12 bits (bits 15:4).
    // Shift right by 4 to get the signed integer.
    int16_t raw12 = (int16_t)raw >> 4;

    // Convert to Celsius: resolution is 0.0625°C per LSB
    temperature = raw12 * 0.0625f;

    return TMP102_SUCCESS;
}

TMP102::tmp102_status_t TMP102::write_register(tmp102_register_t reg, uint16_t value)
{
    char buf[3];
    buf[0] = (char)reg;
    buf[1] = (char)((value >> 8) & 0xFF);   // MSB
    buf[2] = (char)(value & 0xFF);           // LSB

    int ack = _i2c.write(_addr, buf, 3);
    return (ack == 0) ? TMP102_SUCCESS : TMP102_FAILURE;
}

TMP102::tmp102_status_t TMP102::read_register(tmp102_register_t reg, uint16_t &value)
{
    // Set pointer register
    char ptr = (char)reg;
    int ack = _i2c.write(_addr, &ptr, 1, true);
    if (ack != 0) {
        return TMP102_FAILURE;
    }

    // Read 2 bytes (MSB first)
    char buf[2] = {0, 0};
    ack = _i2c.read(_addr, buf, 2);
    if (ack != 0) {
        return TMP102_FAILURE;
    }

    value = ((uint16_t)(uint8_t)buf[0] << 8) | (uint8_t)buf[1];
    return TMP102_SUCCESS;
}
