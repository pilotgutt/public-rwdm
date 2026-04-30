//////////////////////////////////////////////////////////////////////////////////////////
//
//   Mbed Library for ADS1292R Shield/Breakout (NUCLEO-G0B1RE)
//   Converted from Arduino Library
//
//   Copyright (c) 2017 ProtoCentral
//   Adapted for Mbed OS - 2026
//
//////////////////////////////////////////////////////////////////////////////////////////
#include "protocentralAds1292r.h"

// Constructor
ads1292r::ads1292r(PinName mosi, PinName miso, PinName sck, 
                   PinName cs, PinName pwdn, PinName start, PinName drdy)
    : spi(mosi, miso, sck),
      cs_pin(cs),
      pwdn_pin(pwdn),
      start_pin(start),
      drdy_pin(drdy),
      SPI_RX_Buff_Count(0),
      ads1292dataReceived(false)
{
    // Initialize SPI
    spi.format(8, 1);              // 8 bits, mode 1 (CPOL=0, CPHA=1)
    spi.frequency(1000000);         // 1 MHz clock
    
    // Initialize pins - use write() method for DigitalOut
    cs_pin.write(1);
    pwdn_pin.write(1);
    start_pin.write(1);
}

bool ads1292r::getAds1292EcgAndRespirationSamples(ads1292OutputValues *ecgRespirationValues)
{
    if (drdy_pin == 0)  // DRDY is active low
    {
        uint8_t *SPI_RX_Buff_Ptr = ads1292ReadData();
        
        for (int i = 0; i < 9; i++)
        {
            SPI_RX_Buff[SPI_RX_Buff_Count++] = SPI_RX_Buff_Ptr[i];
        }
        
        ads1292dataReceived = true;
        
        unsigned long uecgtemp = 0;
        unsigned long resultTemp = 0;
        signed long secgtemp = 0;
        long statusByte = 0;
        uint8_t LeadStatus = 0;
        
        int j = 0;
        for (int i = 3; i < 9; i += 3)
        {
            uecgtemp = (unsigned long)(((unsigned long)SPI_RX_Buff[i + 0] << 16) |
                                      ((unsigned long)SPI_RX_Buff[i + 1] << 8) |
                                      (unsigned long)SPI_RX_Buff[i + 2]);
            uecgtemp = (unsigned long)(uecgtemp << 8);
            secgtemp = (signed long)(uecgtemp);
            secgtemp = (signed long)(secgtemp >> 8);
            
            (ecgRespirationValues->sDaqVals)[j++] = secgtemp;
        }
        
        statusByte = (long)((long)SPI_RX_Buff[2] | 
                           ((long)SPI_RX_Buff[1] << 8) | 
                           ((long)SPI_RX_Buff[0] << 16));
        statusByte = (statusByte & 0x0f8000) >> 15;
        LeadStatus = (uint8_t)statusByte;
        
        resultTemp = (uint32_t)((0 << 24) | (SPI_RX_Buff[3] << 16) |
                               (SPI_RX_Buff[4] << 8) | SPI_RX_Buff[5]);
        resultTemp = (uint32_t)(resultTemp << 8);
        ecgRespirationValues->sresultTempResp = (long)(resultTemp);
        
        if (!  ((LeadStatus & 0x1f) == 0))
        {
            ecgRespirationValues->leadoffDetected = true;
        }
        else
        {
            ecgRespirationValues->leadoffDetected = false;
        }
        
        ads1292dataReceived = false;
        SPI_RX_Buff_Count = 0;
        return true;
    }
    
    return false;
}

uint8_t* ads1292r::ads1292ReadData()
{
    static uint8_t SPI_Dummy_Buff[10];
    cs_pin.write(0);
    
    for (int i = 0; i < 9; ++i)
    {
        SPI_Dummy_Buff[i] = spi.write(CONFIG_SPI_MASTER_DUMMY);
    }
    
    cs_pin.write(1);
    return SPI_Dummy_Buff;
}

void ads1292r::ads1292Init()
{
    ads1292Reset();
    ThisThread::sleep_for(100ms);
    ads1292DisableStart();
    ads1292EnableStart();
    ads1292HardStop();
    ads1292StartDataConvCommand();
    ads1292SoftStop();
    ThisThread::sleep_for(50ms);
    ads1292StopReadDataContinuous();
    ThisThread::sleep_for(300ms);
    
    ads1292RegWrite(ADS1292_REG_CONFIG1, 0x00);
    ThisThread::sleep_for(10ms);
    ads1292RegWrite(ADS1292_REG_CONFIG2, 0b10100000);
    ThisThread::sleep_for(10ms);
    ads1292RegWrite(ADS1292_REG_LOFF, 0b00010000);
    ThisThread::sleep_for(10ms);
    ads1292RegWrite(ADS1292_REG_CH1SET, 0b01000000);
    ThisThread::sleep_for(10ms);
    ads1292RegWrite(ADS1292_REG_CH2SET, 0b01100000);
    ThisThread::sleep_for(10ms);
    ads1292RegWrite(ADS1292_REG_RLDSENS, 0b00101100);
    ThisThread::sleep_for(10ms);
    ads1292RegWrite(ADS1292_REG_LOFFSENS, 0x00);
    ThisThread::sleep_for(10ms);
    ads1292RegWrite(ADS1292_REG_RESP1, 0b11110010);
    ThisThread::sleep_for(10ms);
    ads1292RegWrite(ADS1292_REG_RESP2, 0b00000011);
    ThisThread::sleep_for(10ms);
    ads1292StartReadDataContinuous();
    ThisThread::sleep_for(10ms);
    ads1292EnableStart();
}

void ads1292r::ads1292Reset()
{
    pwdn_pin.write(1);
    ThisThread::sleep_for(100ms);
    pwdn_pin.write(0);
    ThisThread::sleep_for(100ms);
    pwdn_pin.write(1);
    ThisThread::sleep_for(100ms);
}

void ads1292r::ads1292DisableStart()
{
    start_pin.write(0);
    ThisThread::sleep_for(20ms);
}

void ads1292r::ads1292EnableStart()
{
    start_pin.write(1);
    ThisThread::sleep_for(20ms);
}

void ads1292r::ads1292HardStop()
{
    start_pin.write(0);
    ThisThread::sleep_for(100ms);
}

void ads1292r::ads1292StartDataConvCommand()
{
    ads1292SPICommandData(START);
}

void ads1292r::ads1292SoftStop()
{
    ads1292SPICommandData(STOP);
}

void ads1292r::ads1292StartReadDataContinuous()
{
    ads1292SPICommandData(RDATAC);
}

void ads1292r::ads1292StopReadDataContinuous()
{
    ads1292SPICommandData(SDATAC);
}

void ads1292r::ads1292SPICommandData(uint8_t dataIn)
{
    cs_pin.write(0);
    ThisThread::sleep_for(2ms);
    cs_pin.write(1);
    ThisThread::sleep_for(2ms);
    cs_pin.write(0);
    ThisThread::sleep_for(2ms);
    spi.write(dataIn);
    ThisThread::sleep_for(2ms);
    cs_pin.write(1);
}

void ads1292r::ads1292RegWrite(uint8_t READ_WRITE_ADDRESS, uint8_t DATA)
{
    switch (READ_WRITE_ADDRESS)
    {
        case 1:
            DATA = DATA & 0x87;
            break;
        case 2:
            DATA = DATA & 0xFB;
            DATA |= 0x80;
            break;
        case 3:
            DATA = DATA & 0xFD;
            DATA |= 0x10;
            break;
        case 7:
            DATA = DATA & 0x3F;
            break;
        case 8:
            DATA = DATA & 0x5F;
            break;
        case 9:
            DATA |= 0x02;
            break;
        case 10:
            DATA = DATA & 0x87;
            DATA |= 0x01;
            break;
        case 11:
            DATA = DATA & 0x0F;
            break;
        default:
            break;
    }
    
    uint8_t dataToSend = READ_WRITE_ADDRESS | WREG;
    cs_pin.write(0);
    ThisThread::sleep_for(2ms);
    cs_pin.write(1);
    ThisThread::sleep_for(2ms);
    cs_pin.write(0);
    ThisThread::sleep_for(2ms);
    spi.write(dataToSend);
    spi.write(0x00);
    spi.write(DATA);
    ThisThread::sleep_for(2ms);
    cs_pin.write(1);
}
