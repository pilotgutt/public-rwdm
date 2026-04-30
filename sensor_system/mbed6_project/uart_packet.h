#ifndef UART_PACKET_H
#define UART_PACKET_H

#include "mbed.h"

/**
 * @brief Shared sensor data written by each sensor task and read by the
 *        UART packet task.  Access is protected by g_sensor_mutex.
 *
 * Binary packet layout sent on D1 (PC_4 / UART1_TX) → MAX3232 → RS232:
 *
 *   Byte  0   : 0xAA  (sync 1)
 *   Byte  1   : 0x55  (sync 2)
 *   Byte  2   : 0x20  (payload length = 32 = 8 × 4)
 *   Bytes 3-34: 8 × float32 little-endian, fields in the order below
 *   Byte 35   : XOR checksum of bytes 3-34
 *
 * Field index → name (unit):
 *   0  roll_deg       (°)
 *   1  pitch_deg      (°)
 *   2  heading_deg    (° true)
 *   3  ms_temperature (°C)
 *   4  ms_pressure    (mbar)
 *   5  heart_rate     (BPM)
 *   6  spo2           (%)
 *   7  tmp_temperature(°C)
 */
struct SensorData {
    float roll_deg;
    float pitch_deg;
    float heading_deg;
    float ms_temperature;
    float ms_pressure;
    float heart_rate;
    float spo2;
    float tmp_temperature;
};

extern SensorData g_sensor_data;
extern Mutex      g_sensor_mutex;

void start_uart_packet_thread();

#endif // UART_PACKET_H
