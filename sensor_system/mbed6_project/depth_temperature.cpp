#include "depth_temperature.h"
#include <cstdint>
#include <cstdio>

// ---- MS5837 commands ----
static constexpr uint8_t CMD_RESET = 0x1E;
static constexpr uint8_t CMD_ADC_READ = 0x00;

// OSR=4096 (bra oppløsning). Passer med ventetid ~10ms.
static constexpr uint8_t CMD_CONV_D1_4096 = 0x48; // pressure
static constexpr uint8_t CMD_CONV_D2_4096 = 0x58; // temperature

DepthTemperature::DepthTemperature(I2C &i2c, int address)
    : _i2c(i2c),
      _addr(address << 1),
      D1(0),
      D2(0),
      temperature(0.0f),
      pressure(0.0f),
      seaPressure(1013.25f),
      depth(0.0f) {
    for (int i = 0; i < 7; i++) C[i] = 0;
}

bool DepthTemperature::init() {
    if (!reset()) return false;
    thread_sleep_for(10);

    if (!readPROM()) return false;
    thread_sleep_for(10);

    if (!readSensor()) return false;

    // Kalibrer "seaPressure" til trykket der du startet
    seaPressure = pressure;
    return true;
}

bool DepthTemperature::reset() {
    char cmd = (char)CMD_RESET;
    return (_i2c.write(_addr, &cmd, 1) == 0);
}

// Combine two bytes -> uint16_t
static inline uint16_t to_uint16(uint8_t msb, uint8_t lsb) {
    return (uint16_t)((uint16_t)msb << 8) | (uint16_t)lsb;
}

bool DepthTemperature::readPROM() {
    // MS5837 har C1..C6 i PROM på 0xA2..0xAC (hver 2 byte)
    for (int i = 0; i < 6; i++) {
        uint8_t prom_addr = (uint8_t)(0xA2 + i * 2);
        char cmd = (char)prom_addr;
        char data[2];

        if (_i2c.write(_addr, &cmd, 1) != 0) return false;
        thread_sleep_for(5);
        if (_i2c.read(_addr, data, 2) != 0) return false;

        // data[0]=MSB, data[1]=LSB
        C[i + 1] = to_uint16((uint8_t)data[0], (uint8_t)data[1]);
        //printf("C%d = %u\n", i + 1, (unsigned)C[i + 1]);
    }
    return true;
}

bool DepthTemperature::convert(uint8_t cmd) {
    return (_i2c.write(_addr, (char *)&cmd, 1) == 0);
}

bool DepthTemperature::readADC(uint32_t &value) {
    char cmd = (char)CMD_ADC_READ;
    char data[3];

    if (_i2c.write(_addr, &cmd, 1) != 0) return false;
    if (_i2c.read(_addr, data, 3) != 0) return false;

    value = ((uint32_t)(uint8_t)data[0] << 16) |
            ((uint32_t)(uint8_t)data[1] << 8) |
            (uint32_t)(uint8_t)data[2];
    return true;
}

bool DepthTemperature::readSensor() {
    // --- Read D1 (pressure) ---
    if (!convert(CMD_CONV_D1_4096)) return false;
    thread_sleep_for(10);
    if (!readADC(D1)) return false;

    // --- Read D2 (temperature) ---
    if (!convert(CMD_CONV_D2_4096)) return false;
    thread_sleep_for(10);
    if (!readADC(D2)) return false;

    // ---- MS5837-30BA calculation ----
    // dT = D2 - C5*2^8
    //printf("D1=%lu D2=%lu\n", (unsigned long)D1, (unsigned long)D2);
    int64_t dT = (int64_t)D2 - ((int64_t)C[5] * 256LL);

    // TEMP = 2000 + dT*C6/2^23   (0.01°C)
    int64_t TEMP = 2000LL + (dT * (int64_t)C[6]) / 8388608LL;

    // OFF  = C2*2^16 + C4*dT/2^7
    // SENS = C1*2^15 + C3*dT/2^8
    int64_t OFF  = ((int64_t)C[2] * 65536LL) + ((int64_t)C[4] * dT) / 128LL;
    int64_t SENS = ((int64_t)C[1] * 32768LL) + ((int64_t)C[3] * dT) / 256LL;

    // 2nd order compensation (MS5837-30BA)
    int64_t T2 = 0;
    int64_t OFF2 = 0;
    int64_t SENS2 = 0;

    if (TEMP < 2000) {
        // low temp
        T2 = (3LL * dT * dT) / 8589934592LL;             // 3*dT^2 / 2^33
        OFF2 = (3LL * (TEMP - 2000LL) * (TEMP - 2000LL)) / 2LL;
        SENS2 = (5LL * (TEMP - 2000LL) * (TEMP - 2000LL)) / 8LL;

        if (TEMP < -1500) {
            OFF2 += 7LL * (TEMP + 1500LL) * (TEMP + 1500LL);
            SENS2 += (4LL * (TEMP + 1500LL) * (TEMP + 1500LL));
        }
    } else {
        // high temp
        T2 = (2LL * dT * dT) / 137438953472LL;           // 2*dT^2 / 2^37
        OFF2 = (1LL * (TEMP - 2000LL) * (TEMP - 2000LL)) / 16LL;
        SENS2 = 0;
    }

    TEMP -= T2;
    OFF  -= OFF2;
    SENS -= SENS2;

    // P = (D1*SENS/2^21 - OFF) / 2^13   -> Pa? (skaleres)
    // Datasheet for MS5837-30BA:
    // P (0.01 mbar) = (D1*SENS/2^21 - OFF) / 2^13
    // => mbar = value / 100
    int64_t P = (((int64_t)D1 * SENS) / 2097152LL - OFF) / 8192LL; // P i "0.01 mbar" (ifølge datasheet)
    
    //printf("dT=%lld TEMP=%lld OFF=%lld SENS=%lld\n",
    //   (long long)dT, (long long)TEMP, (long long)OFF, (long long)SENS);

    int64_t P_raw = (((int64_t)D1 * SENS) / 2097152LL - OFF) / 8192LL; // 30BA /2^13
    //printf("P_raw=%lld -> mbar=%f\n", (long long)P_raw, (float)P_raw / 10.0f);
    
    pressure = (float)P / 10.0f;                                 // mbar
    temperature = (float)TEMP / 100.0f;                           // °C

    // Depth (m): (P - P0) / (rho*g)  der P i Pascal
    // Konverter mbar -> Pa: 1 mbar = 100 Pa
    float P_pa = pressure * 100.0f;
    float P0_pa = seaPressure * 100.0f;

    depth = (P_pa - P0_pa) / (1029.0f * 9.80665f);

    return true;
}

float DepthTemperature::getTemperature() { return temperature; }
float DepthTemperature::getPressure() { return pressure; }
float DepthTemperature::getDepth() { return depth; }
uint32_t DepthTemperature::getRawD1() const { return D1; }
uint32_t DepthTemperature::getRawD2() const { return D2; }

// Serial helper unchanged
int DepthTemperature::write_float(char *buf, float v, int decimals) {
    int len = 0;
    if (v < 0) {
        buf[len++] = '-';
        v = -v;
    }
    int ip = (int)v;
    float frac = v - ip;
    len += sprintf(buf + len, "%d.", ip);
    for (int i = 0; i < decimals; i++) {
        frac *= 10.0f;
        int d = (int)frac;
        buf[len++] = '0' + d;
        frac -= d;
    }
    return len;
}