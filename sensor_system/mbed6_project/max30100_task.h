#ifndef MAX30100_TASK_H
#define MAX30100_TASK_H
#include "mbed.h"

extern BufferedSerial pc;
void start_max30100_thread(I2C& i2c, Mutex& i2c_mutex);

#endif
