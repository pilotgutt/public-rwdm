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
#ifndef ads1292r_h
#define ads1292r_h

#include "mbed.h"

#define CONFIG_SPI_MASTER_DUMMY   0xFF

// Register Read Commands
#define RREG  0x20		//Read n nnnn registers starting at address r rrrr
#define WREG  0x40		//Write n nnnn registers starting at address r rrrr
#define START 0x08		//Start/restart (synchronize) conversions
#define STOP  0x0A		//Stop conversion
#define RDATAC 0x10		//Enable Read Data Continuous mode
#define SDATAC 0x11		//Stop Read Data Continuously mode
#define RDATA  0x12		//Read data by command; supports multiple read back

// Register addresses
#define ADS1292_REG_ID       0x00
#define ADS1292_REG_CONFIG1  0x01
#define ADS1292_REG_CONFIG2  0x02
#define ADS1292_REG_LOFF     0x03
#define ADS1292_REG_CH1SET   0x04
#define ADS1292_REG_CH2SET   0x05
#define ADS1292_REG_RLDSENS  0x06
#define ADS1292_REG_LOFFSENS 0x07
#define ADS1292_REG_LOFFSTAT 0x08
#define ADS1292_REG_RESP1    0x09
#define ADS1292_REG_RESP2    0x0A

// Packet format
#define CES_CMDIF_PKT_START_1  0x0A
#define CES_CMDIF_PKT_START_2  0xFA
#define CES_CMDIF_TYPE_DATA    0x02
#define CES_CMDIF_PKT_STOP_1   0x00
#define CES_CMDIF_PKT_STOP_2   0x0B

typedef struct Record {
    volatile signed long sDaqVals[8];
    bool leadoffDetected;
    signed long sresultTempResp;
} ads1292OutputValues;

class ads1292r
{
public:
    // Constructor - initialize with pin configuration
    ads1292r(PinName mosi, PinName miso, PinName sck, 
             PinName cs, PinName pwdn, PinName start_pin, PinName drdy);
    
    bool getAds1292EcgAndRespirationSamples(ads1292OutputValues *ecgRespirationValues);
    void ads1292Init();
    void ads1292Reset();

private:
    // SPI and Pin objects
    SPI spi;
    DigitalOut cs_pin;
    DigitalOut pwdn_pin;
    DigitalOut start_pin;
    DigitalIn drdy_pin;
    
    // Internal buffers
    uint8_t SPI_RX_Buff[15];
    int SPI_RX_Buff_Count;
    bool ads1292dataReceived;
    
    // Helper methods
    void ads1292RegWrite(uint8_t READ_WRITE_ADDRESS, uint8_t DATA);
    void ads1292SPICommandData(uint8_t dataIn);
    void ads1292DisableStart();
    void ads1292EnableStart();
    void ads1292HardStop();
    void ads1292StartDataConvCommand();
    void ads1292SoftStop();
    void ads1292StartReadDataContinuous();
    void ads1292StopReadDataContinuous();
    uint8_t* ads1292ReadData();
};

#endif