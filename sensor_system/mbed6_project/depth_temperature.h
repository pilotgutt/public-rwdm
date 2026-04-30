// depth_temperature.h
#include "mbed.h"

class DepthTemperature {
public:
    DepthTemperature(I2C &i2c, int address = 0x76);

    bool init();            // reset + PROM
    bool readSensor();      // les D1/D2 og kalkuler kompensert verdier

    float getTemperature(); // °C
    float getPressure();    // mbar
    float getDepth();       // meter vann, relativ til referanse

    uint32_t getRawD1() const;   // raw pressure ADC
    uint32_t getRawD2() const;   // raw temp ADC

    int write_float(char *buf, float v, int decimals);

private:
    I2C &_i2c;
    int _addr;

    uint16_t C[7];       // PROM coefficients C1..C6
    uint32_t D1;         // raw pressure
    uint32_t D2;         // raw temperature

    float temperature;   // kompensert °C
    float pressure;      // kompensert mbar
    float seaPressure;   // referansetrykk i luft
    float depth;

    bool reset();
    bool readPROM();
    bool convert(uint8_t cmd);
    bool readADC(uint32_t &value);

};