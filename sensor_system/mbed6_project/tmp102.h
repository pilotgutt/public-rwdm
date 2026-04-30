/**
 * @brief       tmp102.h
 * @details     TMP102 Digital Temperature Sensor driver.
 *              Uses shared I2C bus for compatibility with other sensors.
 *
 * TMP102 I2C address: 0x48 (ADD0 pin connected to GND)
 * Temperature register: 12-bit two's complement, 0.0625°C resolution
 */
#ifndef TMP102_H
#define TMP102_H

#include "mbed.h"

class TMP102 {
public:
    /**
     * @brief Default I2C address (ADD0 = GND -> 0x48, shifted to 8-bit)
     */
    static constexpr uint8_t DEFAULT_ADDR = 0x48 << 1;

    /**
     * @brief TMP102 register pointers (pointer byte)
     */
    typedef enum {
        REG_TEMPERATURE  = 0x00,    /*!< Temperature register (read-only)  */
        REG_CONFIGURATION = 0x01,   /*!< Configuration register (R/W)      */
        REG_T_LOW        = 0x02,    /*!< T_LOW limit register (R/W)        */
        REG_T_HIGH       = 0x03     /*!< T_HIGH limit register (R/W)       */
    } tmp102_register_t;

    /**
     * @brief Status codes
     */
    typedef enum {
        TMP102_SUCCESS = 0,
        TMP102_FAILURE = 1
    } tmp102_status_t;

    /**
     * @brief Create a TMP102 object connected to the specified I2C bus.
     *
     * @param i2c   Reference to the shared I2C object
     * @param addr  8-bit I2C address (default: DEFAULT_ADDR)
     */
    TMP102(I2C &i2c, uint8_t addr = DEFAULT_ADDR);

    /**
     * @brief Initialize the TMP102 (continuous conversion mode, 12-bit resolution).
     * @return TMP102_SUCCESS on success, TMP102_FAILURE otherwise
     */
    tmp102_status_t init();

    /**
     * @brief Read the current temperature.
     * @param temperature  Output: temperature in degrees Celsius
     * @return TMP102_SUCCESS on success, TMP102_FAILURE otherwise
     */
    tmp102_status_t read_temperature(float &temperature);

private:
    I2C    &_i2c;
    uint8_t _addr;

    /**
     * @brief Write to a TMP102 register (pointer byte + 2 data bytes).
     */
    tmp102_status_t write_register(tmp102_register_t reg, uint16_t value);

    /**
     * @brief Read two bytes from the currently pointed-to register.
     */
    tmp102_status_t read_register(tmp102_register_t reg, uint16_t &value);
};

#endif // TMP102_H
