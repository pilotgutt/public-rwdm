#include "uart_packet.h"
#include <cstring>

// Shared sensor data and its mutex (written by sensor tasks, read here)
SensorData g_sensor_data = {};
Mutex      g_sensor_mutex;

// D1 = PC_4 (UART1_TX), PC_5 (UART1_RX) — connected to MAX3232 for RS232
static BufferedSerial rs232(PC_4, PC_5, 115200);

static Thread uart_packet_thread(osPriorityNormal, 2048);

extern BufferedSerial pc; // pc defined in main.cpp

// Packet constants
static const uint8_t SYNC1       = 0xAA;
static const uint8_t SYNC2       = 0x55;
static const uint8_t PAYLOAD_LEN = sizeof(SensorData); // currently 32 bytes (8 floats)
// Total packet size (current): 3 header + 32 payload + 1 checksum = 36 bytes

static void uart_packet_task()
{
    while (true) {
        // Snapshot the shared data atomically
        SensorData snap;
        g_sensor_mutex.lock();
        snap = g_sensor_data;
        g_sensor_mutex.unlock();

        // Build packet: [SYNC1][SYNC2][PAYLOAD_LEN][...payload...][xor_checksum]
        uint8_t pkt[3 + PAYLOAD_LEN + 1];
        pkt[0] = SYNC1;
        pkt[1] = SYNC2;
        pkt[2] = PAYLOAD_LEN;
        memcpy(&pkt[3], &snap, PAYLOAD_LEN);

        // XOR checksum over payload bytes
        uint8_t chk = 0;
        for (uint8_t i = 0; i < PAYLOAD_LEN; i++) {
            chk ^= pkt[3 + i];
        }
        pkt[3 + PAYLOAD_LEN] = chk;

        rs232.write(pkt, sizeof(pkt));

        pc.set_blocking(true);

        char msg[512];
        int offset = snprintf(msg, sizeof(msg), "pkt=");

        for (size_t i = 0; i < sizeof(pkt); i++) {
            offset += snprintf(msg + offset, sizeof(msg) - offset,
                            "%02X ", pkt[i]);
        }

        offset += snprintf(msg + offset, sizeof(msg) - offset,
                        " sizeof(pkt)=%u\r\n", sizeof(pkt));

        pc.write(msg, offset);

        // Send at 10 Hz (100 ms between packets)
        ThisThread::sleep_for(100ms);
    }
}

void start_uart_packet_thread()
{
    uart_packet_thread.start(uart_packet_task);
}
