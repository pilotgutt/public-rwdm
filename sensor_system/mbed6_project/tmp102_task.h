#ifndef TMP102_TASK_H
#define TMP102_TASK_H
#include "mbed.h"

void start_tmp102_thread(I2C& i2c, Mutex& i2c_mutex);

#endif
