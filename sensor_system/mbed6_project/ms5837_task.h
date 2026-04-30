#ifndef MS5837_TASK_H
#define MS5837_TASK_H
#include "mbed.h"

void start_ms5837_thread(I2C& i2c, Mutex& i2c_mutex);

#endif
