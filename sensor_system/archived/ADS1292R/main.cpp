//////////////////////////////////////////////////////////////////////////////////////////
//
//   Mbed Library for ADS1292R Shield/Breakout (NUCLEO-G0B1RE)
//   Converted from Arduino Library
//
//   Copyright (c) 2017 ProtoCentral
//   Adapted for Mbed OS - 2026
//
//   This software is licensed under the MIT License(http://opensource.org/licenses/MIT).
//
//////////////////////////////////////////////////////////////////////////////////////////

#include "mbed.h"
#include "protocentralAds1292r.h"
#include "ecgRespirationAlgo.h"

// Global variables
volatile uint8_t globalHeartRate = 0;
volatile uint8_t globalRespirationRate = 0;

// Pin definitions for NUCLEO-G0B1RE
const PinName ADS1292_MOSI = PA_7;      // SPI MOSI
const PinName ADS1292_MISO = PA_6;      // SPI MISO
const PinName ADS1292_SCK = PA_5;       // SPI SCK
const PinName ADS1292_CS = PB_0;        // Chip Select
const PinName ADS1292_PWDN = PA_1;      // Power Down / Reset
const PinName ADS1292_START = PA_0;     // Start Input
const PinName ADS1292_DRDY = PA_8;      // Data Ready Output

// Data packet format
#define CES_CMDIF_PKT_START_1   0x0A
#define CES_CMDIF_PKT_START_2   0xFA
#define CES_CMDIF_TYPE_DATA     0x02
#define CES_CMDIF_PKT_STOP      0x0B
#define DATA_LEN                9
#define ZERO                    0

char DataPacket[DATA_LEN];
const char DataPacketFooter[2] = {ZERO, CES_CMDIF_PKT_STOP};
const char DataPacketHeader[5] = {CES_CMDIF_PKT_START_1, CES_CMDIF_PKT_START_2, DATA_LEN, ZERO, CES_CMDIF_TYPE_DATA};

int16_t ecgWaveBuff, ecgFilterout;
int16_t resWaveBuff, respFilterout;

// Create ADS1292R instance
ads1292r ADS1292R(ADS1292_MOSI, ADS1292_MISO, ADS1292_SCK, 
                  ADS1292_CS, ADS1292_PWDN, ADS1292_START, ADS1292_DRDY);

// Create ECG/Respiration algorithm instance
ecg_respiration_algorithm ECG_RESPIRATION_ALGORITHM;

// Serial port for debugging and data transmission
UnbufferedSerial serial_port(USBTX, USBRX, 115200);

void sendDataThroughUART()
{
    DataPacket[0] = ecgFilterout;
    DataPacket[1] = ecgFilterout >> 8;
    DataPacket[2] = resWaveBuff;
    DataPacket[3] = resWaveBuff >> 8;
    DataPacket[4] = globalRespirationRate;
    DataPacket[5] = globalRespirationRate >> 8;
    DataPacket[6] = globalHeartRate;
    DataPacket[7] = globalHeartRate >> 8;
    DataPacket[8] = 0;
    
    // Send packet header
    for (int i = 0; i < 5; i++)
    {
        serial_port.write((const void*)&DataPacketHeader[i], 1);
    }
    
    // Send data
    for (int i = 0; i < DATA_LEN; i++)
    {
        serial_port.write((const void*)&DataPacket[i], 1);
    }
    
    // Send packet footer
    for (int i = 0; i < 2; i++)
    {
        serial_port.write((const void*)&DataPacketFooter[i], 1);
    }
}

int main()
{
    // Initialize
    ThisThread::sleep_for(2s);
    
    printf("\n\n=== ADS1292R ECG/Respiration Monitor ===\n");
    printf("Initializing ADS1292R...\n");
    
    ADS1292R.ads1292Init();
    
    printf("Initialization complete\n");
    printf("Waiting for data.. .\n\n");
    
    int sample_count = 0;
    
    while (true)
    {
        ads1292OutputValues ecgRespirationValues;
        ecgRespirationValues.leadoffDetected = true;
        
        if (ADS1292R.getAds1292EcgAndRespirationSamples(&ecgRespirationValues))
        {
            ecgWaveBuff = (int16_t)(ecgRespirationValues. sDaqVals[1] >> 8);
            resWaveBuff = (int16_t)(ecgRespirationValues.sresultTempResp >> 8);
            
            // Debug output - print raw values
            if (sample_count % 100 == 0)
            {
                printf("[%d] Raw ECG: %ld | Raw Resp: %ld | LeadOff: %d\n",
                       sample_count,
                       ecgRespirationValues.sDaqVals[1],
                       ecgRespirationValues.sresultTempResp,
                       ecgRespirationValues.leadoffDetected);
            }
            
            if (ecgRespirationValues.leadoffDetected == false)
            {
                ECG_RESPIRATION_ALGORITHM.ECG_ProcessCurrSample(&ecgWaveBuff, &ecgFilterout);
                ECG_RESPIRATION_ALGORITHM.QRS_Algorithm_Interface(ecgFilterout, &globalHeartRate);
                
                // Uncomment to enable respiration rate calculation
                respFilterout = ECG_RESPIRATION_ALGORITHM. Resp_ProcessCurrSample(resWaveBuff);
                ECG_RESPIRATION_ALGORITHM. RESP_Algorithm_Interface(respFilterout, &globalRespirationRate);
            }
            else
            {
                if (sample_count % 100 == 0)
                {
                    printf("  ⚠ LEAD OFF DETECTED - No electrodes connected?\n");
                }
                ecgFilterout = 0;
                respFilterout = 0;
            }
            
            sendDataThroughUART();
            sample_count++;
        }
        else
        {
            // DRDY not ready yet
            ThisThread::sleep_for(1ms);
        }
    }
    
    return 0;
}
