#include "mbed.h"
#include "uart_packet.h"
#include <cmath>
#include <cstdint>
#include <cstring>

// RTOS thread for LSM9DS1 IMU sensor
static Thread lsm9ds1_thread(osPriorityNormal, 8192);

// References to shared I2C and mutex (set in start_lsm9ds1_thread)
static I2C*   i2c_ptr       = nullptr;
static Mutex* i2c_mutex_ptr = nullptr;

extern BufferedSerial pc; // pc defined in main.cpp

// ---------------- I2C 7-bit addresses ----------------
static constexpr uint8_t AG_ADDR_7B = 0x6B; // accel+gyro
static constexpr uint8_t M_ADDR_7B  = 0x1E; // magnetometer

// ---------------- Registers: Accel/Gyro (AG) ----------------
static constexpr uint8_t WHO_AM_I_AG  = 0x0F;
static constexpr uint8_t CTRL_REG1_G  = 0x10;
static constexpr uint8_t CTRL_REG6_XL = 0x20;
static constexpr uint8_t CTRL_REG8    = 0x22;
static constexpr uint8_t OUT_X_L_G    = 0x18;
static constexpr uint8_t OUT_X_L_XL   = 0x28;

// ---------------- Registers: Magnetometer (M) ----------------
static constexpr uint8_t WHO_AM_I_M   = 0x0F;
static constexpr uint8_t CTRL_REG1_M  = 0x20;
static constexpr uint8_t CTRL_REG2_M  = 0x21;
static constexpr uint8_t CTRL_REG3_M  = 0x22;
static constexpr uint8_t OUT_X_L_M    = 0x28;

// ---------------- Helpers ----------------
static inline int16_t le16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline float rad2deg(float r) { return r * 57.2957795131f; }

static inline float wrap360(float deg)
{
    while (deg < 0.0f) deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    return deg;
}

// ---------------- I2C helpers (use shared bus via i2c_ptr) ----------------
static int imu_write_reg(uint8_t addr7, uint8_t reg, uint8_t val)
{
    char data[2] = { (char)reg, (char)val };
    return i2c_ptr->write(addr7 << 1, data, 2);
}

static int imu_read_regs(uint8_t addr7, uint8_t start_reg, uint8_t *buf, int len)
{
    char reg = (char)start_reg;
    int r = i2c_ptr->write(addr7 << 1, &reg, 1, true);
    if (r != 0) return r;
    return i2c_ptr->read(addr7 << 1, (char *)buf, len);
}

// ---------------- Declination ----------------
static constexpr float DECLINATION_DEG_HORTEN = 4.79f;

// ---------------- Magnetometer calibration ----------------
static constexpr float MAG_B[3] = {613.69f, 452.57f, 1546.26f};

static constexpr float MAG_AINV[3][3] = {
{ 0.346 , 0.01266 , 0.00263 },
{ 0.01266 , 0.33909 , 0.0016 },
{ 0.00263 , 0.0016 , 0.35766 }};


static inline void calibrate_mag(float mx_raw, float my_raw, float mz_raw,
                                 float &mx_cal, float &my_cal, float &mz_cal)
{
    const float x = mx_raw - MAG_B[0];
    const float y = my_raw - MAG_B[1];
    const float z = mz_raw - MAG_B[2];

    mx_cal = MAG_AINV[0][0]*x + MAG_AINV[0][1]*y + MAG_AINV[0][2]*z;
    my_cal = MAG_AINV[1][0]*x + MAG_AINV[1][1]*y + MAG_AINV[1][2]*z;
    mz_cal = MAG_AINV[2][0]*x + MAG_AINV[2][1]*y + MAG_AINV[2][2]*z;
}

// ---------------- Gyro bias (RAW counts) ----------------
static int16_t GYRO_BIAS_RAW[3] = { 144, -24, -76 };

// ---------------- IMU initialisation ----------------
static bool init_lsm9ds1()
{
    uint8_t who_ag = 0, who_m = 0;

    i2c_mutex_ptr->lock();
    int r1 = imu_read_regs(AG_ADDR_7B, WHO_AM_I_AG, &who_ag, 1);
    int r2 = imu_read_regs(M_ADDR_7B,  WHO_AM_I_M,  &who_m,  1);
    i2c_mutex_ptr->unlock();

    if (r1 != 0 || r2 != 0) return false;

    char msg[64];
    int n = snprintf(msg, sizeof(msg), "LSM9DS1 WHO_AM_I AG=0x%02X, M=0x%02X\r\n", who_ag, who_m);
    pc.write(msg, n);

    i2c_mutex_ptr->lock();
    // AG: enable address auto-increment for multi-byte reads
    imu_write_reg(AG_ADDR_7B, CTRL_REG8,    0x04);
    // Gyro: 238 Hz, ±500 dps
    imu_write_reg(AG_ADDR_7B, CTRL_REG1_G,  0x68);
    // Accel: 238 Hz, ±4 g
    imu_write_reg(AG_ADDR_7B, CTRL_REG6_XL, 0x68);
    // Mag: 80 Hz, continuous, ±4 gauss
    imu_write_reg(M_ADDR_7B,  CTRL_REG1_M,  0x7C);
    imu_write_reg(M_ADDR_7B,  CTRL_REG2_M,  0x00);
    int ret = imu_write_reg(M_ADDR_7B,  CTRL_REG3_M,  0x00);
    i2c_mutex_ptr->unlock();

    return (ret == 0);
}

// ---------------- RTOS task ----------------
static void lsm9ds1_task()
{
    pc.set_blocking(true);
    ThisThread::sleep_for(20ms);

    if (!init_lsm9ds1()) {
        pc.write("LSM9DS1 init failed\r\n", 21);
        return;
    }
    pc.write("LSM9DS1 Initialized\r\n", 21);

    // Scale factors
    const float GYRO_DPS_PER_LSB = 0.0175f;   // ±500 dps
    const float ACC_G_PER_LSB    = 0.000732f;  // ±4 g
    const float MAG_UT_PER_LSB   = 0.014f;

    Timer t;
    t.start();
    uint32_t last_ms = 0;
    static float heading_filtered = 0.0f;

    while (true) {
        uint8_t gb[6], ab[6], mb[6];

        i2c_mutex_ptr->lock();
        int rg = imu_read_regs(AG_ADDR_7B, OUT_X_L_G,               gb, 6);
        int ra = imu_read_regs(AG_ADDR_7B, OUT_X_L_XL,              ab, 6);
        int rm = imu_read_regs(M_ADDR_7B,  (uint8_t)(OUT_X_L_M), mb, 6);
        i2c_mutex_ptr->unlock();

        if (rg == 0 && ra == 0 && rm == 0) {
            int16_t gx_raw = le16(&gb[0]), gy_raw = le16(&gb[2]), gz_raw = le16(&gb[4]);
            int16_t ax_raw = le16(&ab[0]), ay_raw = le16(&ab[2]), az_raw = le16(&ab[4]);
            int16_t mx_raw = le16(&mb[0]), my_raw = le16(&mb[2]), mz_raw = le16(&mb[4]);

            // Subtract gyro bias then convert
            float gx_dps = (gx_raw - GYRO_BIAS_RAW[0]) * GYRO_DPS_PER_LSB;
            float gy_dps = (gy_raw - GYRO_BIAS_RAW[1]) * GYRO_DPS_PER_LSB;
            float gz_dps = (gz_raw - GYRO_BIAS_RAW[2]) * GYRO_DPS_PER_LSB;

            float ax_g = ax_raw * ACC_G_PER_LSB;
            float ay_g = ay_raw * ACC_G_PER_LSB;
            float az_g = az_raw * ACC_G_PER_LSB;

            // Magnetometer: calibrated
            float mx_c, my_c, mz_c;
            calibrate_mag((float)mx_raw, (float)my_raw, (float)mz_raw, mx_c, my_c, mz_c);

            float mx_uT = mx_raw * MAG_UT_PER_LSB;
            float my_uT = my_raw * MAG_UT_PER_LSB;
            float mz_uT = mz_raw * MAG_UT_PER_LSB;

            // Roll & Pitch
            float roll_rad  = atan2f(-ay_g, az_g);
            float pitch_rad = atan2f(ax_g, sqrtf(ay_g*ay_g + az_g*az_g));

            float roll_deg  = rad2deg(roll_rad);
            float pitch_deg = rad2deg(pitch_rad);

            // Tilt compensation
            float cr = cosf(roll_rad);
            float sr = sinf(roll_rad);
            float cp = cosf(pitch_rad);
            float sp = sinf(pitch_rad);

            float Xh = mx_c * cp + mz_c * sp;
            float Yh = mx_c * sr * sp + my_c * cr - mz_c * sr * cp;

            // Heading
            float heading_mag_deg  = wrap360(rad2deg(atan2f(Yh, Xh)));

            // 180 deg fix (if needed)
            heading_mag_deg = wrap360(heading_mag_deg + 180.0f);

            static float heading_filtered = 0.0f;
            static bool first_run = true;

            float heading_true_deg = wrap360(heading_mag_deg + DECLINATION_DEG_HORTEN);

            if (first_run) {
                heading_filtered = heading_true_deg;
                first_run = false;
            }

            float alpha = 0.1f;

            // finn korteste vei mellom vinklene
            float diff = heading_true_deg - heading_filtered;

            if (diff > 180.0f)  diff -= 360.0f;
            if (diff < -180.0f) diff += 360.0f;

            heading_filtered += alpha * diff;
            heading_filtered = wrap360(heading_filtered);

            heading_true_deg = heading_filtered;

            // Update shared sensor data for RS232 binary packet
            g_sensor_mutex.lock();
            g_sensor_data.roll_deg    = roll_deg;
            g_sensor_data.pitch_deg   = pitch_deg;
            g_sensor_data.heading_deg = heading_true_deg;
            g_sensor_mutex.unlock();

            // Print at 10 Hz
            uint32_t now_ms = (uint32_t)(t.elapsed_time().count() / 1000);
            if (now_ms - last_ms >= 100) {
                last_ms = now_ms;
/*
                char msg[320];
                int n = snprintf(
                    msg, sizeof(msg),
                    "LSM9DS1: Roll:%7.2f  Pitch:%7.2f  Yaw:%7.2f  A(g):%6.3f %6.3f %6.3f \r\n",
                    roll_deg, pitch_deg, heading_true_deg, ax_g, ay_g, az_g
                );
                pc.write(msg, n);
*/
            }
        } else {
            pc.write("LSM9DS1 I2C read error\r\n", 24);
            ThisThread::sleep_for(200ms);
        }

        ThisThread::sleep_for(100ms);
    }
}

void start_lsm9ds1_thread(I2C& i2c, Mutex& i2c_mutex)
{
    i2c_ptr       = &i2c;
    i2c_mutex_ptr = &i2c_mutex;
    lsm9ds1_thread.start(lsm9ds1_task);
}