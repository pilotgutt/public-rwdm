#include "mbed.h"
#include "depth_temperature.h"
#include "uart_packet.h"

// RTOS thread for MS5837 pressure/temperature sensor
static Thread ms5837_thread(osPriorityNormal, 4096);

// References to shared I2C and mutex (set in start_ms5837_thread)
static I2C*   i2c_ptr       = nullptr;
static Mutex* i2c_mutex_ptr = nullptr;

extern BufferedSerial pc; // pc defined in main.cpp

static void ms5837_task()
{
    pc.set_blocking(true);

    // Create DepthTemperature object with the shared I2C bus
    DepthTemperature sensor(*i2c_ptr);

    // Initialize sensor (reset + PROM + first read)
    i2c_mutex_ptr->lock();
    bool ok = sensor.init();
    i2c_mutex_ptr->unlock();

    if (!ok) {
        pc.write("MS5837 init failed\r\n", 20);
    } else {
        pc.write("MS5837 Initialized\r\n", 20);
    }

    char txbuf[128];

    while (true) {
        // Lock mutex before I2C operations
        i2c_mutex_ptr->lock();
        bool read_ok = sensor.readSensor();
        i2c_mutex_ptr->unlock();

        if (read_ok) {
            g_sensor_mutex.lock();
            g_sensor_data.ms_temperature = sensor.getTemperature();
            g_sensor_data.ms_pressure    = sensor.getPressure();
            g_sensor_mutex.unlock();

/*
            int len = snprintf(txbuf, sizeof(txbuf),
                "MS5837 Temp: %.2f C | Press: %.2f mbar | Depth: %.2f m\r\n",
                sensor.getTemperature(),
                sensor.getPressure(),
                sensor.getDepth());
            pc.write(txbuf, len);
*/
        } else {
            //pc.write("MS5837 read error\r\n", 19);
        }

        // Read every 1000 ms
        ThisThread::sleep_for(1000ms);
    }
}

void start_ms5837_thread(I2C& i2c, Mutex& i2c_mutex)
{
    i2c_ptr       = &i2c;
    i2c_mutex_ptr = &i2c_mutex;
    ms5837_thread.start(ms5837_task);
}
