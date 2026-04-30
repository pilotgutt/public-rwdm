#include "mbed.h"
#include "tmp102.h"
#include "uart_packet.h"

// RTOS thread for TMP102 temperature sensor
static Thread tmp102_thread(osPriorityNormal, 4096);

// References to shared I2C and mutex (set in start_tmp102_thread)
static I2C*   i2c_ptr       = nullptr;
static Mutex* i2c_mutex_ptr = nullptr;

extern BufferedSerial pc; // pc defined in main.cpp

static void tmp102_task()
{
    pc.set_blocking(true);

    // Create TMP102 object with the shared I2C bus
    TMP102 tmp102(*i2c_ptr);

    // Initialize sensor (continuous conversion, 12-bit resolution)
    i2c_mutex_ptr->lock();
    TMP102::tmp102_status_t status = tmp102.init();
    i2c_mutex_ptr->unlock();

    if (status != TMP102::TMP102_SUCCESS) {
        pc.write("TMP102 init failed\r\n", 20);
    } else {
        pc.write("TMP102 Initialized\r\n", 20);
    }

    char txbuf[64];

    while (true) {
        float temperature = 0.0f;

        // Lock mutex before I2C operations
        i2c_mutex_ptr->lock();
        status = tmp102.read_temperature(temperature);
        i2c_mutex_ptr->unlock();

        if (status == TMP102::TMP102_SUCCESS) {
            g_sensor_mutex.lock();
            g_sensor_data.tmp_temperature = temperature;
            g_sensor_mutex.unlock();

            //int len = snprintf(txbuf, sizeof(txbuf), "TMP102 Temp: %.4f C\r\n", temperature);
            //pc.write(txbuf, len);
        } else {
            //pc.write("TMP102 read error\r\n", 19);
        }

        // Read every 500 ms (TMP102 default conversion rate is 4 Hz; 500 ms reduces I2C bus traffic)
        ThisThread::sleep_for(500ms);
    }
}

void start_tmp102_thread(I2C& i2c, Mutex& i2c_mutex)
{
    i2c_ptr       = &i2c;
    i2c_mutex_ptr = &i2c_mutex;
    tmp102_thread.start(tmp102_task);
}
