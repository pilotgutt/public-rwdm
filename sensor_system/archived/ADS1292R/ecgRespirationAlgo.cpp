//////////////////////////////////////////////////////////////////////////////////////////
//
//   Mbed Library for ADS1292R Shield/Breakout (NUCLEO-G0B1RE)
//   ECG and Respiration Algorithm Implementation
//
//   Copyright (c) 2017 ProtoCentral
//   Adapted for Mbed OS - 2026
//
//   This software is licensed under the MIT License(http://opensource.org/licenses/MIT).
//
//////////////////////////////////////////////////////////////////////////////////////////

#include "mbed.h"
#include "ecgRespirationAlgo.h"

// Global variables declaration (match the header declarations)
unsigned char Start_Sample_Count_Flag = 0;
unsigned char first_peak_detect = FALSE;

unsigned int sample_count = 0;
unsigned int sample_index[MAX_PEAK_TO_SEARCH + 2];

int QRS_Second_Prev_Sample = 0;
int QRS_Prev_Sample = 0;
int QRS_Current_Sample = 0;
int QRS_Next_Sample = 0;
int QRS_Second_Next_Sample = 0;
int RESP_Second_Prev_Sample = 0;
int RESP_Prev_Sample = 0;
int RESP_Current_Sample = 0;
int RESP_Next_Sample = 0;
int RESP_Second_Next_Sample = 0;

uint8_t Respiration_Rate = 0;

volatile uint16_t QRS_Heart_Rate = 0;
static uint16_t QRS_B4_Buffer_ptr = 0;
int16_t RESP_WorkingBuff[2 * FILTERORDER];
int16_t Pvev_DC_Sample = 0;
int16_t Pvev_Sample = 0;
int16_t QRS_Threshold_Old = 0;
int16_t QRS_Threshold_New = 0;
int16_t ECG_WorkingBuff[2 * FILTERORDER];

int16_t CoeffBuf_40Hz_LowPass[FILTERORDER] = {-72,    122,    -31,    -99,    117,      0,   -121,    105,     34,
                                             -137,     84,     70,   -146,     55,    104,   -147,     20,    135,
                                             -137,    -21,    160,   -117,    -64,    177,    -87,   -108,    185,
                                              -48,   -151,    181,      0,   -188,    164,     54,   -218,    134,
                                              112,   -238,     90,    171,   -244,     33,    229,   -235,    -36,
                                              280,   -208,   -115,    322,   -161,   -203,    350,    -92,   -296,
                                              361,      0,   -391,    348,    117,   -486,    305,    264,   -577,
                                              225,    445,   -660,     93,    676,   -733,   -119,    991,   -793,
                                             -480,   1486,   -837,  -1226,   2561,   -865,  -4018,   9438,  20972,
                                             9438,  -4018,   -865,   2561,  -1226,   -837,   1486,   -480,   -793,
                                              991,   -119,   -733,    676,     93,   -660,    445,    225,   -577,
                                              264,    305,   -486,    117,    348,   -391,      0,    361,   -296,
                                              -92,    350,   -203,   -161,    322,   -115,   -208,    280,    -36,
                                             -235,    229,     33,   -244,    171,     90,   -238,    112,    134,
                                             -218,     54,    164,   -188,      0,    181,   -151,    -48,    185,
                                             -108,    -87,    177,    -64,   -117,    160,    -21,   -137,    135,
                                               20,   -147,    104,     55,   -146,     70,     84,   -137,     34,
                                              105,   -121,      0,    117,    -99,    -31,    122,    -72       };

int16_t RespCoeffBuf[FILTERORDER] = { 120,    124,    126,    127,    127,    125,    122,    118,    113,
                                      106,     97,     88,     77,     65,     52,     38,     24,      8,
                                       -8,    -25,    -42,    -59,    -76,    -93,   -110,   -126,   -142,
                                     -156,   -170,   -183,   -194,   -203,   -211,   -217,   -221,   -223,
                                     -223,   -220,   -215,   -208,   -198,   -185,   -170,   -152,   -132,
                                     -108,    -83,    -55,    -24,      8,     43,     80,    119,    159,
                                      201,    244,    288,    333,    378,    424,    470,    516,    561,
                                      606,    650,    693,    734,    773,    811,    847,    880,    911,
                                      939,    964,    986,   1005,   1020,   1033,   1041,   1047,   1049,
                                     1047,   1041,   1033,   1020,   1005,    986,    964,    939,    911,
                                      880,    847,    811,    773,    734,    693,    650,    606,    561,
                                      516,    470,    424,    378,    333,    288,    244,    201,    159,
                                      119,     80,     43,      8,    -24,    -55,    -83,   -108,   -132,
                                     -152,   -170,   -185,   -198,   -208,   -215,   -220,   -223,   -223,
                                     -221,   -217,   -211,   -203,   -194,   -183,   -170,   -156,   -142,
                                     -126,   -110,    -93,    -76,    -59,    -42,    -25,     -8,      8,
                                       24,     38,     52,     65,     77,     88,     97,    106,    113,
                                      118,    122,    125,    127,    127,    126,    124,    120       };

// ============================================================================
// ECG FILTER PROCESS
// ============================================================================
void ecg_respiration_algorithm:: ECG_FilterProcess(int16_t * WorkingBuff, int16_t * CoeffBuf, int16_t* FilterOut)
{
    int32_t acc = 0;   // accumulator for MACs
    int k;
    
    // Perform the multiply-accumulate
    for (k = 0; k < 161; k++)
    {
        acc += (int32_t)(*CoeffBuf++) * (int32_t)(*WorkingBuff--);
    }
    
    // Saturate the result
    if (acc > 0x3fffffff)
    {
        acc = 0x3fffffff;
    }
    else if (acc < -0x40000000)
    {
        acc = -0x40000000;
    }
    
    // Convert from Q30 to Q15
    *FilterOut = (int16_t)(acc >> 15);
}

// ============================================================================
// ECG PROCESS CURRENT SAMPLE
// ============================================================================
void ecg_respiration_algorithm::ECG_ProcessCurrSample(int16_t *CurrAqsSample, int16_t *FilteredOut)
{
    static uint16_t ECG_bufStart = 0;
    static uint16_t ECG_bufCur = FILTERORDER - 1;
    static uint8_t ECGFirstFlag = 1;
    static int16_t ECG_Pvev_DC_Sample = 0;
    static int16_t ECG_Pvev_Sample = 0;
    
    int16_t *CoeffBuf;
    int16_t temp1, temp2, ECGData;
    uint16_t Cur_Chan;
    int16_t FiltOut = 0;
    
    CoeffBuf = CoeffBuf_40Hz_LowPass;
    
    // First time initialization
    if (ECGFirstFlag)
    {
        for (Cur_Chan = 0; Cur_Chan < FILTERORDER; Cur_Chan++)
        {
            ECG_WorkingBuff[Cur_Chan] = 0;
        }
        
        ECG_Pvev_DC_Sample = 0;
        ECG_Pvev_Sample = 0;
        ECGFirstFlag = 0;
    }
    
    // First order IIR DC removal
    temp1 = (int16_t)(NRCOEFF * ECG_Pvev_DC_Sample);
    ECG_Pvev_DC_Sample = (CurrAqsSample[0] - ECG_Pvev_Sample) + temp1;
    ECG_Pvev_Sample = CurrAqsSample[0];
    temp2 = ECG_Pvev_DC_Sample >> 2;
    ECGData = (int16_t)temp2;
    
    // Store the DC removed value in working buffer
    ECG_WorkingBuff[ECG_bufCur] = ECGData;
    ECG_FilterProcess(&ECG_WorkingBuff[ECG_bufCur], CoeffBuf, (int16_t*)&FiltOut);
    
    // Store DC removed value in ECG_WorkingBuff buffer
    ECG_WorkingBuff[ECG_bufStart] = ECGData;
    
    // Store filtered output sample
    FilteredOut[0] = FiltOut;
    
    // Update buffer pointers
    ECG_bufCur++;
    ECG_bufStart++;
    
    if (ECG_bufStart == (FILTERORDER - 1))
    {
        ECG_bufStart = 0;
        ECG_bufCur = FILTERORDER - 1;
    }
}

// ============================================================================
// QRS ALGORITHM INTERFACE
// ============================================================================
void ecg_respiration_algorithm::QRS_Algorithm_Interface(int16_t CurrSample, volatile uint8_t *Heart_rate)
{
    static int16_t prev_data[32] = {0};
    int16_t i;
    long Mac = 0;
    
    prev_data[0] = CurrSample;
    
    // Moving average filter
    for (i = 31; i > 0; i--)
    {
        Mac += prev_data[i];
        prev_data[i] = prev_data[i - 1];
    }
    
    Mac += CurrSample;
    Mac = Mac >> 2;
    CurrSample = (int16_t)Mac;
    
    // Shift samples
    QRS_Second_Prev_Sample = QRS_Prev_Sample;
    QRS_Prev_Sample = QRS_Current_Sample;
    QRS_Current_Sample = QRS_Next_Sample;
    QRS_Next_Sample = QRS_Second_Next_Sample;
    QRS_Second_Next_Sample = CurrSample;
    
    QRS_process_buffer(Heart_rate);
}

// ============================================================================
// QRS PROCESS BUFFER
// ============================================================================
void ecg_respiration_algorithm::QRS_process_buffer(volatile uint8_t *Heart_rate)
{
    int16_t first_derivative = 0;
    int16_t scaled_result = 0;
    static int16_t Max = 0;
    
    // Calculate first derivative
    first_derivative = QRS_Next_Sample - QRS_Prev_Sample;
    
    // Take absolute value
    if (first_derivative < 0)
    {
        first_derivative = -(first_derivative);
    }
    
    scaled_result = first_derivative;
    
    if (scaled_result > Max)
    {
        Max = scaled_result;
    }
    
    QRS_B4_Buffer_ptr++;
    
    // Every 2 seconds (250 samples @ 125 SPS)
    if (QRS_B4_Buffer_ptr == TWO_SEC_SAMPLES)
    {
        QRS_Threshold_Old = ((Max * 7) / 10);
        QRS_Threshold_New = QRS_Threshold_Old;
        first_peak_detect = TRUE;
        Max = 0;
        QRS_B4_Buffer_ptr = 0;
    }
    
    if (TRUE == first_peak_detect)
    {
        QRS_check_sample_crossing_threshold(scaled_result, Heart_rate);
    }
}

// ============================================================================
// QRS CHECK SAMPLE CROSSING THRESHOLD
// ============================================================================
void ecg_respiration_algorithm::QRS_check_sample_crossing_threshold(uint16_t scaled_result, volatile uint8_t *Heart_rate)
{
    static uint16_t s_array_index = 0;
    static uint16_t m_array_index = 0;
    static unsigned char threshold_crossed = FALSE;
    static uint16_t maxima_search = 0;
    static unsigned char peak_detected = FALSE;
    static uint16_t skip_window = 0;
    static long maxima_sum = 0;
    static unsigned int peak = 0;
    static unsigned int sample_sum = 0;
    static unsigned int nopeak = 0;
    
    uint16_t Max = 0;
    uint16_t HRAvg;
    uint16_t RRinterval = 0;
    
    if (TRUE == threshold_crossed)
    {
        // Check for maxima value until MAXIMA_SEARCH_WINDOW samples received
        sample_count++;
        maxima_search++;
        
        if (scaled_result > peak)
        {
            peak = scaled_result;
        }
        
        if (maxima_search >= MAXIMA_SEARCH_WINDOW)
        {
            // Store maxima values for each peak
            maxima_sum += peak;
            maxima_search = 0;
            threshold_crossed = FALSE;
            peak_detected = TRUE;
        }
    }
    else if (TRUE == peak_detected)
    {
        // Sample goes below threshold - skip until SKIP WINDOW criteria met
        sample_count++;
        skip_window++;
        
        if (skip_window >= MINIMUM_SKIP_WINDOW)
        {
            skip_window = 0;
            peak_detected = FALSE;
        }
        
        if (m_array_index == MAX_PEAK_TO_SEARCH)
        {
            sample_sum = sample_sum / (MAX_PEAK_TO_SEARCH - 1);
            HRAvg = (uint16_t)sample_sum;
            
            // Compute HR
            QRS_Heart_Rate = (uint16_t)(60 * SAMPLING_RATE);
            QRS_Heart_Rate = QRS_Heart_Rate / HRAvg;
            
            if (QRS_Heart_Rate > 250)
            {
                QRS_Heart_Rate = 250;
            }
            
            // Calculate new QRS_Threshold based on maxima from peaks
            maxima_sum = maxima_sum / MAX_PEAK_TO_SEARCH;
            Max = (int16_t)maxima_sum;
            
            maxima_sum = Max * 7;
            maxima_sum = maxima_sum / 10;
            QRS_Threshold_New = (int16_t)maxima_sum;
            
            // Limit QRS Threshold to permissible range
            if (QRS_Threshold_New > (4 * QRS_Threshold_Old))
            {
                QRS_Threshold_New = QRS_Threshold_Old;
            }
            
            // Reset variables
            sample_count = 0;
            s_array_index = 0;
            m_array_index = 0;
            maxima_sum = 0;
            sample_index[0] = 0;
            sample_index[1] = 0;
            sample_index[2] = 0;
            sample_index[3] = 0;
            Start_Sample_Count_Flag = 0;
            sample_sum = 0;
        }
    }
    else if (scaled_result > QRS_Threshold_New)
    {
        // Sample crosses threshold - store sample index
        Start_Sample_Count_Flag = 1;
        sample_count++;
        m_array_index++;
        threshold_crossed = TRUE;
        peak = scaled_result;
        nopeak = 0;
        
        // Store sample index
        sample_index[s_array_index] = sample_count;
        
        if (s_array_index >= 1)
        {
            sample_sum += sample_index[s_array_index] - sample_index[s_array_index - 1];
        }
        
        s_array_index++;
    }
    else if ((scaled_result < QRS_Threshold_New) && (Start_Sample_Count_Flag == 1))
    {
        sample_count++;
        nopeak++;
        
        if (nopeak > (3 * SAMPLING_RATE))
        {
            // Reset if no peak found in 3 seconds
            sample_count = 0;
            s_array_index = 0;
            m_array_index = 0;
            maxima_sum = 0;
            sample_index[0] = 0;
            sample_index[1] = 0;
            sample_index[2] = 0;
            sample_index[3] = 0;
            Start_Sample_Count_Flag = 0;
            peak_detected = FALSE;
            sample_sum = 0;
            first_peak_detect = FALSE;
            nopeak = 0;
            QRS_Heart_Rate = 0;
        }
    }
    else
    {
        nopeak++;
        
        if (nopeak > (3 * SAMPLING_RATE))
        {
            // Reset heart rate computation state variables
            sample_count = 0;
            s_array_index = 0;
            m_array_index = 0;
            maxima_sum = 0;
            sample_index[0] = 0;
            sample_index[1] = 0;
            sample_index[2] = 0;
            sample_index[3] = 0;
            Start_Sample_Count_Flag = 0;
            peak_detected = FALSE;
            sample_sum = 0;
            first_peak_detect = FALSE;
            nopeak = 0;
            QRS_Heart_Rate = 0;
        }
    }
    
    *Heart_rate = (uint8_t)QRS_Heart_Rate;
}

// ============================================================================
// RESPIRATION FILTER PROCESS
// ============================================================================
void ecg_respiration_algorithm:: Resp_FilterProcess(int16_t * RESP_WorkingBuff, int16_t * CoeffBuf, int16_t* FilterOut)
{
    int32_t acc = 0;
    int k;
    
    // Perform multiply-accumulate
    for (k = 0; k < 161; k++)
    {
        acc += (int32_t)(*CoeffBuf++) * (int32_t)(*RESP_WorkingBuff--);
    }
    
    // Saturate result
    if (acc > 0x3fffffff)
    {
        acc = 0x3fffffff;
    }
    else if (acc < -0x40000000)
    {
        acc = -0x40000000;
    }
    
    // Convert from Q30 to Q15
    *FilterOut = (int16_t)(acc >> 15);
}

// ============================================================================
// RESPIRATION PROCESS CURRENT SAMPLE
// ============================================================================
int16_t ecg_respiration_algorithm::Resp_ProcessCurrSample(int16_t CurrAqsSample)
{
    static uint16_t bufStart = 0;
    static uint16_t bufCur = FILTERORDER - 1;
    static uint8_t FirstFlag = 1;
    
    int16_t temp1, temp2;
    int16_t RESPData;
    uint16_t Cur_Chan;
    int16_t FiltOut;
    
    // First order IIR DC removal
    temp1 = (int16_t)(NRCOEFF * Pvev_DC_Sample);
    Pvev_DC_Sample = (CurrAqsSample - Pvev_Sample) + temp1;
    Pvev_Sample = CurrAqsSample;
    temp2 = Pvev_DC_Sample;
    RESPData = (int16_t)temp2;
    
    RESPData = CurrAqsSample;
    
    // Store DC removed value in RESP_WorkingBuff buffer
    RESP_WorkingBuff[bufCur] = RESPData;
    Resp_FilterProcess(&RESP_WorkingBuff[bufCur], RespCoeffBuf, (int16_t*)&FiltOut);
    
    // Store DC removed value in working buffer
    RESP_WorkingBuff[bufStart] = RESPData;
    
    // Update buffer pointers
    bufCur++;
    bufStart++;
    
    if (bufStart >= (FILTERORDER - 1))
    {
        bufStart = 0;
        bufCur = FILTERORDER - 1;
    }
    
    return FiltOut;
}

// ============================================================================
// RESPIRATION ALGORITHM INTERFACE
// ============================================================================
void ecg_respiration_algorithm:: RESP_Algorithm_Interface(int16_t CurrSample, volatile uint8_t *RespirationRate)
{
    static int16_t prev_data[64] = {0};
    char i;
    long Mac = 0;
    
    prev_data[0] = CurrSample;
    
    // Moving average filter
    for (i = 63; i > 0; i--)
    {
        Mac += prev_data[i];
        prev_data[i] = prev_data[i - 1];
    }
    
    Mac += CurrSample;
    CurrSample = (int16_t)(Mac >> 1);
    
    // Shift samples
    RESP_Second_Prev_Sample = RESP_Prev_Sample;
    RESP_Prev_Sample = RESP_Current_Sample;
    RESP_Current_Sample = RESP_Next_Sample;
    RESP_Next_Sample = RESP_Second_Next_Sample;
    RESP_Second_Next_Sample = CurrSample;
    
    Respiration_Rate_Detection(RESP_Second_Next_Sample, RespirationRate);
}

// ============================================================================
// RESPIRATION RATE DETECTION
// ============================================================================
void ecg_respiration_algorithm::Respiration_Rate_Detection(int16_t Resp_wave, volatile uint8_t *RespirationRate)
{
    static uint16_t skipCount = 0;
    static uint16_t SampleCount = 0;
    static uint16_t TimeCnt = 0;
    static uint16_t SampleCountNtve = 0;
    static uint16_t PtiveCnt = 0;
    static uint16_t NtiveCnt = 0;
    
    static int16_t MinThreshold = 0x7FFF;
    static int16_t MaxThreshold = 0x8000;
    static int16_t PrevSample = 0;
    static int16_t PrevPrevSample = 0;
    static int16_t PrevPrevPrevSample = 0;
    
    static int16_t MinThresholdNew = 0x7FFF;
    static int16_t MaxThresholdNew = 0x8000;
    static int16_t AvgThreshold = 0;
    
    static unsigned char startCalc = 0;
    static unsigned char PtiveEdgeDetected = 0;
    static unsigned char NtiveEdgeDetected = 0;
    static unsigned char peakCount = 0;
    static uint16_t PeakCount[8];
    
    SampleCount++;
    SampleCountNtve++;
    TimeCnt++;
    
    if (Resp_wave < MinThresholdNew)
    {
        MinThresholdNew = Resp_wave;
    }
    
    if (Resp_wave > MaxThresholdNew)
    {
        MaxThresholdNew = Resp_wave;
    }
    
    if (SampleCount > 1000)
    {
        SampleCount = 0;
    }
    
    if (SampleCountNtve > 1000)
    {
        SampleCountNtve = 0;
    }
    
    if (startCalc == 1)
    {
        if (TimeCnt >= 500)
        {
            TimeCnt = 0;
            
            if ((MaxThresholdNew - MinThresholdNew) > 400)
            {
                MaxThreshold = MaxThresholdNew;
                MinThreshold = MinThresholdNew;
                AvgThreshold = MaxThreshold + MinThreshold;
                AvgThreshold = AvgThreshold >> 1;
            }
            else
            {
                startCalc = 0;
                Respiration_Rate = 0;
            }
        }
        
        PrevPrevPrevSample = PrevPrevSample;
        PrevPrevSample = PrevSample;
        PrevSample = Resp_wave;
        
        if (skipCount == 0)
        {
            if (PrevPrevPrevSample < AvgThreshold && Resp_wave > AvgThreshold)
            {
                if (SampleCount > 40 && SampleCount < 700)
                {
                    PtiveEdgeDetected = 1;
                    PtiveCnt = SampleCount;
                    skipCount = 4;
                }
                
                SampleCount = 0;
            }
            
            if (PrevPrevPrevSample < AvgThreshold && Resp_wave > AvgThreshold)
            {
                if (SampleCountNtve > 40 && SampleCountNtve < 700)
                {
                    NtiveEdgeDetected = 1;
                    NtiveCnt = SampleCountNtve;
                    skipCount = 4;
                }
                
                SampleCountNtve = 0;
            }
            
            if (PtiveEdgeDetected == 1 && NtiveEdgeDetected == 1)
            {
                PtiveEdgeDetected = 0;
                NtiveEdgeDetected = 0;
                
                if (abs(PtiveCnt - NtiveCnt) < 5)
                {
                    PeakCount[peakCount++] = PtiveCnt;
                    PeakCount[peakCount++] = NtiveCnt;
                    
                    if (peakCount == 8)
                    {
                        peakCount = 0;
                        PtiveCnt = PeakCount[0] + PeakCount[1] + PeakCount[2] + PeakCount[3] +
                                   PeakCount[4] + PeakCount[5] + PeakCount[6] + PeakCount[7];
                        PtiveCnt = PtiveCnt >> 3;
                        Respiration_Rate = 6000 / PtiveCnt;
                    }
                }
            }
        }
        else
        {
            skipCount--;
        }
    }
    else
    {
        TimeCnt++;
        
        if (TimeCnt >= 500)
        {
            TimeCnt = 0;
            
            if ((MaxThresholdNew - MinThresholdNew) > 400)
            {
                startCalc = 1;
                MaxThreshold = MaxThresholdNew;
                MinThreshold = MinThresholdNew;
                AvgThreshold = MaxThreshold + MinThreshold;
                AvgThreshold = AvgThreshold >> 1;
                PrevPrevPrevSample = Resp_wave;
                PrevPrevSample = Resp_wave;
                PrevSample = Resp_wave;
            }
        }
    }
    
    *RespirationRate = (uint8_t)Respiration_Rate;
}
