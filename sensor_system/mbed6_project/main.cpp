#include "mbed.h"
#include "max30100_task.h"
#include "tmp102_task.h"
#include "lsm9ds1_task.h"
#include "ms5837_task.h"
#include "uart_packet.h"

DigitalOut myLed(LED1);
BufferedSerial pc(USBTX, USBRX, 115200);

// Centralized I2C instance and mutex for thread-safe access
I2C i2c(I2C_SDA, I2C_SCL);
Mutex i2c_mutex;

// I2C Scanner function
void scan_i2c_devices(I2C& i2c) {
    printf("\n=== I2C Device Scanner ===\n");
    printf("Scanning I2C bus for devices...\n\n");
    
    int devices_found = 0;
    for (int addr = 1; addr < 127; addr++) {
        int ack = i2c.write(addr << 1, NULL, 0);
        if (ack == 0) {
            printf("Device found at: 0x%02X (7-bit: 0x%02X)\n", addr << 1, addr);
            devices_found++;
        }
    }
    
    printf("\nTotal devices found: %d\n", devices_found);
    printf("Your IMU should be at: 0xD6 (7-bit: 0x6A)\n");
    printf("Your MAX30100 should be at: 0xAE (7-bit: 0x57)\n");
    printf("Your TMP102 should be at: 0x90 (7-bit: 0x48)\n");
    printf("Your MS5837 should be at: 0xEC (7-bit: 0x76)\n\n");
}

int main()
{
    // Set I2C frequency to 100kHz for compatibility with both sensors
    i2c.frequency(100000);

    // Scan for devices FIRST
    scan_i2c_devices(i2c);
    
    start_max30100_thread(i2c, i2c_mutex);
    start_tmp102_thread(i2c, i2c_mutex);
    start_lsm9ds1_thread(i2c, i2c_mutex);
    start_ms5837_thread(i2c, i2c_mutex);
    start_uart_packet_thread();

    while (true) {
        
        myLed = !myLed;
        ThisThread::sleep_for(1s);
    }
}

