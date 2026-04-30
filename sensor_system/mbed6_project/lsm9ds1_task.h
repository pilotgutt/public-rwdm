#ifndef LSM9DS1_TASK_H
#define LSM9DS1_TASK_H
#include "mbed.h"

void start_lsm9ds1_thread(I2C& i2c, Mutex& i2c_mutex);

#endif
