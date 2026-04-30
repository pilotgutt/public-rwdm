#include "mbed.h"
#include "max30100.h"
#include "uart_packet.h"

// RTOS Thread variable with increased stack size
static Thread max30100_thread(osPriorityNormal, 8192);

// Reference to shared I2C and MAX30100 objects
static I2C* i2c_ptr = nullptr;
static Mutex* i2c_mutex_ptr = nullptr;
static MAX30100* myMAX30100_ptr = nullptr;

extern BufferedSerial pc; //pc defined in main.cpp

Ticker      newReading; // The Ticker interface is used to setup a recurring interrupt to repeatedly call a function at a specified rate.

MAX30100:: MAX30100_status_t         aux;
MAX30100::MAX30100_vector_data_t    myMAX30100_Data;
volatile uint32_t                   myState = 0;  // volatile since modified in ISR

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================
#define SAMPLE_BUFFER_SIZE      16
#define READING_INTERVAL_MS     200     // Read FIFO every 200ms for better HR detection
#define SAMPLES_PER_SECOND      100     // Must match SPO2_CONFIGURATION_SPO2_SR setting
#define MS_PER_SAMPLE           (1000 / SAMPLES_PER_SECOND)

// Heart rate detection thresholds
#define MIN_IR_THRESHOLD        10000   // Minimum IR value to detect finger presence
#define MIN_SIGNAL_AMPLITUDE    100     // Minimum peak-to-peak amplitude for valid signal (lowered from 200 for weak near-saturation signals)
#define PEAK_THRESHOLD_FACTOR   0.5f    // Peak must be above this fraction of signal range (midpoint; relaxed from 0.6)
#define MIN_BEAT_INTERVAL_MS    300     // Minimum 300ms between beats (200 BPM max)
#define MAX_BEAT_INTERVAL_MS    2000    // Maximum 2000ms between beats (30 BPM min)
#define MIN_VALID_INTERVALS     2       // Minimum intervals needed for HR calculation (2 intervals = 3 peaks for faster first reading)
#define DC_IIR_ALPHA            0.005f  // IIR coefficient for DC baseline tracker (τ ≈ 1/α ≈ 200 samples ≈ 2 s at 100 Hz)
#define DC_WARMUP_SAMPLES       SAMPLES_PER_SECOND  // Ignore peaks for the first 100 samples (1 s) after reset so the IIR baseline settles before beat intervals are recorded

// SpO2 thresholds
#define MIN_RED_THRESHOLD       1000    // Minimum RED value for SpO2 calculation
#define MIN_AC_AMPLITUDE        10      // Minimum AC component for valid SpO2

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================



// Sample storage
// Array of 16 values each of 16 bits
uint16_t IR_samples[SAMPLE_BUFFER_SIZE];
uint16_t RED_samples[SAMPLE_BUFFER_SIZE];

// declaration of variables
// Heart rate detection variables
uint32_t elapsed_time_ms = 0;
uint32_t last_peak_time_ms = 0;
float beat_intervals[8];                // Store last 8 beat intervals for averaging
uint8_t interval_index = 0;
uint8_t valid_intervals = 0;
int32_t ir_max_adaptive = 0;
int32_t ir_min_adaptive = 65535;
int32_t prev_ir_value = 0;
int32_t prev_prev_ir_value = 0;
uint32_t samples_since_peak = 0;
bool looking_for_peak = true;
float ir_dc_ema = 0.0f;              // IIR-filtered DC baseline used as adaptive peak threshold
uint32_t ir_dc_warmup_samples = 0;  // Counts samples since last reset; peaks ignored until >= DC_WARMUP_SAMPLES
uint32_t total_beats_detected = 0;  // Monotonically increasing count of valid peaks (not capped at 8)

// Filtered values for smoother readings
float filtered_heart_rate = 0.0f;
float filtered_spo2 = 0.0f;
#define FILTER_ALPHA 0.3f               // Low-pass filter coefficient for heart rate (0-1, lower = more smoothing)
#define SPO2_FILTER_ALPHA 0.15f         // Low-pass coefficient for SpO2: smoother than HR (0.3) but still responsive

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Check if finger is present on sensor
 * @param ir_avg Average IR value from recent samples
 * @return true if finger detected, false otherwise
 */
bool is_finger_present(float ir_avg) {
    return ir_avg > MIN_IR_THRESHOLD;
}

/**
 * @brief Simple low-pass filter for smoothing values
 * @param current_filtered Current filtered value
 * @param new_value New raw value to incorporate
 * @param alpha Filter coefficient (0-1)
 * @return New filtered value
 */

// glatting av signal -> First-order IIR filter
float low_pass_filter(float current_filtered, float new_value, float alpha) {
    if (new_value <= 0) return current_filtered;  // Ignore invalid values
    if (current_filtered <= 0) return new_value;  // Initialize with first valid value
    return current_filtered + alpha * (new_value - current_filtered);
}

// ============================================================================
// HEART RATE DETECTION
// ============================================================================

/**
 * @brief Reset heart rate detection state
 */
void reset_heart_rate_detection(void) {
    valid_intervals = 0;
    interval_index = 0;
    ir_max_adaptive = 0;
    ir_min_adaptive = 65535;
    samples_since_peak = 0;
    looking_for_peak = true;
    ir_dc_ema = 0.0f;
    ir_dc_warmup_samples = 0;
    total_beats_detected = 0;
    last_peak_time_ms = elapsed_time_ms;
    
    for (int i = 0; i < 8; i++) {
        beat_intervals[i] = 0;
    }
}

/**
 * @brief Improved peak detection with adaptive threshold
 * @param ir_value Current IR sample value
 * @param current_time_ms Current time in milliseconds
 * @return Returns 1 if a valid peak was detected, 0 otherwise
 */
uint8_t detect_pulse_peak(uint16_t ir_value, uint32_t current_time_ms) {
    uint8_t peak_detected = 0;
    
    // Update adaptive min/max
    if ((int32_t)ir_value > ir_max_adaptive) ir_max_adaptive = ir_value;
    if ((int32_t)ir_value < ir_min_adaptive) ir_min_adaptive = ir_value;
    
    samples_since_peak++;
    
    // Slowly decay the range to adapt to signal changes (every ~1 second)
    if (samples_since_peak > SAMPLES_PER_SECOND) {
        int32_t range = ir_max_adaptive - ir_min_adaptive;
        ir_max_adaptive = ir_max_adaptive - (range >> 5);  // Decay by ~3%
        ir_min_adaptive = ir_min_adaptive + (range >> 5);
        samples_since_peak = 0;  // Reset to 0 so next decay fires only after another 100 samples (~1 s),
                                  // preventing every-sample decay that collapsed the adaptive range to near-zero
    }
    
    int32_t range = ir_max_adaptive - ir_min_adaptive;

    // Track the DC baseline with a slow IIR filter so the threshold follows
    // finger-placement drift instead of being anchored to the all-time adaptive
    // midpoint (which can sit well above current cardiac peaks after DC drift).
    if (ir_dc_ema == 0.0f) {
        ir_dc_ema = (float)ir_value;          // initialise on first sample after reset
    } else {
        ir_dc_ema += DC_IIR_ALPHA * ((float)ir_value - ir_dc_ema);
    }
    int32_t threshold = (int32_t)ir_dc_ema;   // peaks must cross the running DC mean

    // Advance warmup counter; no saturation needed (uint32_t overflow takes years at 100 Hz)
    ir_dc_warmup_samples++;

    // Detect peak:  value was rising, now falling, and above threshold
    // Skip peak detection during warmup so transient placement samples do not
    // produce a spuriously short first beat interval.
    if (ir_dc_warmup_samples < DC_WARMUP_SAMPLES) {
        // Update history before returning so prev values are always fresh
        prev_prev_ir_value = prev_ir_value;
        prev_ir_value = ir_value;
        return 0;
    }

    if (looking_for_peak) {
        // Check for peak: previous value is higher than both neighbors
        if ((prev_ir_value > prev_prev_ir_value) && 
            (prev_ir_value > (int32_t)ir_value) && 
            (prev_ir_value > threshold) &&
            (range > MIN_SIGNAL_AMPLITUDE)) {
            
            uint32_t interval = current_time_ms - last_peak_time_ms;
            
            // Validate beat interval is within physiological range
            if (interval >= MIN_BEAT_INTERVAL_MS && interval <= MAX_BEAT_INTERVAL_MS) {
                // Store valid interval in circular buffer
                beat_intervals[interval_index] = (float)interval;
                interval_index = (interval_index + 1) % 8;
                if (valid_intervals < 8) valid_intervals++;
                total_beats_detected++;
                
                peak_detected = 1;
                samples_since_peak = 0;
            }
            
            last_peak_time_ms = current_time_ms;
            looking_for_peak = false;
        }
    } else {
        // Wait for signal to drop below threshold before looking for next peak
        // This prevents detecting multiple peaks on the same heartbeat
        if ((int32_t)ir_value < threshold) {
            looking_for_peak = true;
        }
    }

    // Update history after peak detection so prev_ir_value still holds the
    // previous sample (t-1) when the local-maximum check above is evaluated.
    prev_prev_ir_value = prev_ir_value;
    prev_ir_value = ir_value;
    
    return peak_detected;
}

/**
 * @brief Calculate heart rate from stored beat intervals
 * @return Heart rate in BPM, or 0 if insufficient data
 */
float calculate_heart_rate(void) {
    if (valid_intervals < MIN_VALID_INTERVALS) {
        return 0.0f;  // Need minimum intervals for reliable calculation
    }
    
    // Calculate average interval with outlier rejection
    // count = How many good beat intervals do we have?
    float sum = 0;
    uint8_t count = 0;
    
    // First pass: calculate mean
    // check if beat_intervals is on between min and max pulse we can measure
    for (uint8_t i = 0; i < valid_intervals; i++) {
        if (beat_intervals[i] >= MIN_BEAT_INTERVAL_MS && 
            beat_intervals[i] <= MAX_BEAT_INTERVAL_MS) {
            sum += beat_intervals[i];
            count++;
        }
    }
    
    if (count < 2) return 0.0f;
    
    // gjennomsnittlig intervall mellom slag
    float mean = sum / count;
    
    // Second pass: reject outliers (more than 30% from mean)
    sum = 0;
    count = 0;
    float tolerance = mean * 0.3f;
    
    // iterate through valid_intervals to remove outliers
    for (uint8_t i = 0; i < valid_intervals; i++) {
        float diff = beat_intervals[i] - mean;
        if (diff < 0) diff = -diff;  // Absolute value
        
        if (diff <= tolerance) {
            sum += beat_intervals[i];
            count++;
        }
    }
    
    if (count < 2) {
        // Fall back to mean without outlier removal
        // if too many outliers
        float avg_interval_ms = mean;
        // BPM = 60000 / (time between beats in ms). 60000=ms per minute
        return 60000.0f / avg_interval_ms; 
    }
    
    float avg_interval_ms = sum / count;
    float bpm = 60000.0f / avg_interval_ms;
    
    // Final sanity check
    if (bpm < 30.0f || bpm > 200.0f) return 0.0f;
    
    return bpm;
}

// ============================================================================
// SPO2 CALCULATION
// ============================================================================

/**
 * @brief Calculate SpO2 using the ratio of ratios (R) method
 * 
 * The principle:  SpO2 is calculated from the ratio R = (AC_red/DC_red) / (AC_ir/DC_ir)
 * where AC is the pulsatile component and DC is the baseline component. 
 * 
 * @param ir_samples Array of IR sample values
 * @param red_samples Array of RED sample values
 * @param num_samples Number of samples in arrays
 * @return SpO2 percentage (0 if invalid/no finger)
 */
float calculate_spo2(uint16_t* ir_samples, uint16_t* red_samples, uint8_t num_samples) {
    if (num_samples < 4) return 0.0f;
    
    // Calculate DC components (mean values)
    float ir_dc = 0.0f;
    float red_dc = 0.0f;

    for (uint8_t i = 0; i < num_samples; i++) {
        ir_dc += ir_samples[i];
        red_dc += red_samples[i];
    }
    ir_dc /= num_samples;
    red_dc /= num_samples;
    
    // Check for finger presence
    if (ir_dc < MIN_IR_THRESHOLD || red_dc < MIN_RED_THRESHOLD) {
        return 0.0f;
    }
    
    // Calculate AC components using peak-to-peak method
    float ir_max_val = 0; 
    float ir_min_val = 65535; 
    float red_max_val = 0;
    float red_min_val = 65535;
    
    // iterate through num_samples to find extremes
    for (uint8_t i = 0; i < num_samples; i++) {
        if (ir_samples[i] > ir_max_val) ir_max_val = ir_samples[i];
        if (ir_samples[i] < ir_min_val) ir_min_val = ir_samples[i];
        if (red_samples[i] > red_max_val) red_max_val = red_samples[i];
        if (red_samples[i] < red_min_val) red_min_val = red_samples[i];
    }
    
    // peak to peak calculations
    float ir_ac = ir_max_val - ir_min_val;
    float red_ac = red_max_val - red_min_val;
    
    // Validate AC components (need visible pulse)
    // to avoid noise/disturbance. Need high enough amplitude to
    // be classed as a heart beat.
    if (ir_ac < MIN_AC_AMPLITUDE || red_ac < MIN_AC_AMPLITUDE) {
        return 0.0f;
    }
    
    // Avoid division by zero
    if (ir_dc < 1.0f) return 0.0f;
    
    /*R measures how differently oxygenated blood absorbs RED light compared to IR light, using only the pulsating part of the signal.
      That difference is what tells us the oxygen saturation (SpO₂).
        Raw values depend on:  

        Finger size

        Skin color

        LED brightness

        Sensor placement

        Pressure
      Ratios cancel out most of these effects.
      */

    // Calculate R value (ratio of ratios)
    // R = (AC_red / DC_red) / (AC_ir / DC_ir)
    // Rearranged: R = (AC_red * DC_ir) / (AC_ir * DC_red)
    // The relative absorption difference caused by oxygenation is the r_value
    float r_value = (red_ac * ir_dc) / (ir_ac * red_dc);
    
    // Empirical formula for SpO2 from R value
    // Standard approximation: SpO2 = 110 - 25 * R
    // These coefficients should ideally be calibrated with a reference device
    // Common alternative formulas:
    //   SpO2 = 104 - 17 * R
    //   SpO2 = 110 - 25 * R (used here)
    //   SpO2 = a0 + a1*R + a2*R^2 (quadratic fit for higher accuracy)
    
    float spo2 = 110.0f - 25.0f * r_value;
    
    // Constrain to physiologically valid range
    // Values below 70% are rarely seen in living patients
    // Values above 100% are physically impossible
    if (spo2 > 100.0f) spo2 = 100.0f;
    if (spo2 < 70.0f) spo2 = 70.0f;
    
    return spo2;
}

// ============================================================================
// TICKER CALLBACK
// ============================================================================

void changeDATA(void) {
    myState = 1;
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

static void max30100_task()
{
    float myIRaverage  = 0;
    float myREDaverage = 0;
    float myHeartRate  = 0;
    float mySpO2       = 0;
    
    pc.set_blocking(true); 

    // ========================================================================
    // INITIALIZATION SEQUENCE
    // ========================================================================
  
    // Software reset
    i2c_mutex_ptr->lock();
    aux = myMAX30100_ptr->MAX30100_SoftwareReset();
    i2c_mutex_ptr->unlock();
    ThisThread::sleep_for(10ms); //keep

    // Wait until the software reset is finished
    // (with timeout protection)
    uint32_t reset_timeout = 0;
    do {
        i2c_mutex_ptr->lock();
        aux = myMAX30100_ptr->MAX30100_PollingSoftwareReset(&myMAX30100_Data);
        i2c_mutex_ptr->unlock();
        reset_timeout++;
        ThisThread::sleep_for(10ms);
        if (reset_timeout > 100) {
            break;
        }
    } while((myMAX30100_Data.ResetFlag & MAX30100:: MODE_CONFIGURATION_RESET_MASK) != MAX30100::MODE_CONFIGURATION_RESET_DISABLE);

    // Shutdown enabled (before configuration)
    i2c_mutex_ptr->lock();
    aux = myMAX30100_ptr->MAX30100_ShutdownControl(MAX30100::MODE_CONFIGURATION_SHDN_ENABLE);
    i2c_mutex_ptr->unlock();
    ThisThread::sleep_for(100ms); //keep

    // Get Revision ID
    i2c_mutex_ptr->lock();
    aux = myMAX30100_ptr->MAX30100_GetRevisionID(&myMAX30100_Data);

    // Get Part ID
    aux = myMAX30100_ptr->MAX30100_GetPartID(&myMAX30100_Data);
    i2c_mutex_ptr->unlock();

    // ========================================================================
    // LED CURRENT CONFIGURATION
    // ========================================================================

    // Set RED LED current:  27.1mA (not max to avoid saturation of RED value)
    i2c_mutex_ptr->lock();
    aux = myMAX30100_ptr->MAX30100_SetRed_LED_CurrentControl(MAX30100::LED_CONFIGURATION_RED_PA_27_1_MA);
    // Set IR LED current: 27.1mA (not max to avoid saturation of IR value)
    aux = myMAX30100_ptr->MAX30100_SetIR_LED_CurrentControl(MAX30100:: LED_CONFIGURATION_IR_PA_27_1_MA);
    i2c_mutex_ptr->unlock();

    // ========================================================================
    // SPO2 CONFIGURATION
    // ========================================================================

    // Set sample rate:  100 samples per second
    i2c_mutex_ptr->lock();
    aux = myMAX30100_ptr->MAX30100_SpO2_SampleRateControl(MAX30100::SPO2_CONFIGURATION_SPO2_SR_100);

    // Set pulse width/resolution: 1600us, 16-bit for best resolution
    myMAX30100_Data.Resolution = MAX30100::SPO2_CONFIGURATION_LED_PW_1600US_16BITS;
    aux = myMAX30100_ptr->MAX30100_LED_PulseWidthControl(myMAX30100_Data);

    // Mode Control: SpO2 and HR
    aux = myMAX30100_ptr->MAX30100_ModeControl(MAX30100::MODE_CONFIGURATION_MODE_SPO2_ENABLED);
    i2c_mutex_ptr->unlock();
    ThisThread::sleep_for(100ms); //keep

    // Reset the FIFO
    i2c_mutex_ptr->lock();
    aux = myMAX30100_ptr->MAX30100_ClearFIFO(&myMAX30100_Data);

    // Enable FIFO almost full interrupt
    aux = myMAX30100_ptr->MAX30100_InterrupEnable(
        MAX30100::INTERRUPT_ENABLE_ENB_A_FULL_ENABLE | 
        MAX30100::INTERRUPT_ENABLE_ENB_TEP_RDY_DISABLE | 
        MAX30100:: INTERRUPT_ENABLE_ENB_HR_RDY_DISABLE | 
        MAX30100:: INTERRUPT_ENABLE_ENB_SO2_RDY_DISABLE
    );

    // Shutdown disabled 
    aux = myMAX30100_ptr->MAX30100_ShutdownControl(MAX30100::MODE_CONFIGURATION_SHDN_DISABLE);
    i2c_mutex_ptr->unlock();
    ThisThread::sleep_for(100ms);

    // Initialize heart rate detection
    reset_heart_rate_detection();

    // Attach ticker - faster interval for better heart rate detection
    // newReading = Ticker..Se øverst i kode. Attach er feedback funskjon
    // som kjører changeData hvert READING_INTERVAL_MS
    newReading.attach(&changeDATA, std::chrono::milliseconds(READING_INTERVAL_MS));

    ThisThread::sleep_for(100ms);
    
    bool finger_was_present = false;
    
    // ========================================================================
    // MAIN LOOP
    // ========================================================================
    while(true) {
        // time sleep to avoid CPU constantly checking myState
        ThisThread::sleep_for(50ms);

        if (myState == 1) {
            // ================================================================
            // READ SENSOR DATA
            // ================================================================
            
            // Lock mutex for entire sensor read operation to ensure atomicity
            i2c_mutex_ptr->lock();
            
            // Trigger temperature reading (optional, for monitoring)
            aux = myMAX30100_ptr->MAX30100_TriggerTemperature();

            // Wait for temperature conversion with timeout
            uint32_t temp_timeout = 0;
            do {
                aux = myMAX30100_ptr->MAX30100_PollingTemperatureConversion(&myMAX30100_Data);
                temp_timeout++;
                ThisThread::sleep_for(5ms);
                if (temp_timeout > 200) {
                    break;
                }
            } while((myMAX30100_Data.TemperatureFlag & MAX30100::MODE_CONFIGURATION_TEMP_EN_MASK) == MAX30100::MODE_CONFIGURATION_TEMP_EN_ENABLE);

            // Read temperature
            aux = myMAX30100_ptr->MAX30100_GetTemperature(&myMAX30100_Data);

            // Read the FIFO (16 samples)
            aux = myMAX30100_ptr->MAX30100_ReadFIFO(&myMAX30100_Data, 16);

            // Read interrupt status to clear flags
            aux = myMAX30100_ptr->MAX30100_ReadInterruptStatus(&myMAX30100_Data);
            
            i2c_mutex_ptr->unlock();

            // ================================================================
            // PROCESS SAMPLES
            // ================================================================
            
            // Calculate averages (DC components)
            myIRaverage  = 0;
            myREDaverage = 0;
            
            for (uint32_t i = 0; i < 16; i++) {
                myIRaverage  += myMAX30100_Data.FIFO_IR_samples[i];
                myREDaverage += myMAX30100_Data.FIFO_RED_samples[i];
                
                // Store samples for SpO2 calculation
                IR_samples[i] = myMAX30100_Data.FIFO_IR_samples[i];
                RED_samples[i] = myMAX30100_Data.FIFO_RED_samples[i];
                
                // Process each sample for heart rate detection with timing
                elapsed_time_ms += MS_PER_SAMPLE;
                detect_pulse_peak(myMAX30100_Data.FIFO_IR_samples[i], elapsed_time_ms);
            }

            // Calculate averages
            myIRaverage /= 16.0f;
            myREDaverage /= 16.0f;
            
            // ================================================================
            // CALCULATE VITALS
            // ================================================================
            
            bool finger_present = is_finger_present(myIRaverage);
            
            if (finger_present) {
                // Finger detected - calculate vitals
                
                // Reset detection if finger was just placed
                if (! finger_was_present) { // finger just appeared!
                    reset_heart_rate_detection();
                    filtered_heart_rate = 0;
                    filtered_spo2 = 0;
                }
                
                // Calculate raw values
                float raw_spo2 = calculate_spo2(IR_samples, RED_samples, 16);
                float raw_hr = calculate_heart_rate();


                // Apply low-pass filter for stable readings
                if (raw_spo2 > 0) {
                    // Single-stage first-order IIR with SPO2_FILTER_ALPHA (0.15):
                    // smoother than heart rate (0.3) while staying responsive for the GUI
                    filtered_spo2 = low_pass_filter(filtered_spo2, raw_spo2, SPO2_FILTER_ALPHA);
                    mySpO2 = filtered_spo2;
                } else {
                    mySpO2 = 0;
                }
                
                if (raw_hr > 0) {
                    filtered_heart_rate = low_pass_filter(filtered_heart_rate, raw_hr, FILTER_ALPHA);
                    myHeartRate = filtered_heart_rate;

                } else {
                    myHeartRate = 0;
                }
                
            } else {
                // No finger detected
                myHeartRate = 0;
                mySpO2 = 0;
                filtered_heart_rate = 0;
                filtered_spo2 = 0;
                reset_heart_rate_detection();
            }
                char txbuf[256];
                // Update shared sensor data for RS232 binary packet
                g_sensor_mutex.lock();
                g_sensor_data.heart_rate = filtered_heart_rate;
                g_sensor_data.spo2       = filtered_spo2;
                g_sensor_mutex.unlock();
/*
                int len = snprintf(
                    txbuf, sizeof(txbuf),
                    "MAX30100: IR: %.0f | RED: %.0f | Beats: %lu | HR: %.0f | Spo2: %.0f\r\n",
                    myIRaverage,
                    myREDaverage,
                    (unsigned long)total_beats_detected,
                    myHeartRate,
                    mySpO2
                );
                pc.write((const uint8_t*)txbuf, len);
*/
            
            finger_was_present = finger_present;
            myState = 0;
        }

    } ThisThread::sleep_for(1s);
}

void start_max30100_thread(I2C& i2c, Mutex& i2c_mutex)
{
    i2c_ptr = &i2c;
    i2c_mutex_ptr = &i2c_mutex;
    
    // Create MAX30100 object with shared I2C
    static MAX30100 myMAX30100(i2c, MAX30100::MAX30100_ADDRESS);
    myMAX30100_ptr = &myMAX30100;
    
    max30100_thread.start(max30100_task);
}