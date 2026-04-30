#include "mbed.h"

// SPI pins for NUCLEO-G0B1RE
SPI spi(PA_7, PA_6, PA_5);   // MOSI, MISO, SCK
DigitalOut cs(PB_0);        // Chip Select

BufferedSerial pc(USBTX, USBRX, 115200);

int main()
{
    uint8_t rx[2];
    uint16_t adc_value;

    // SPI configuration for MCP3201
    spi.format(8, 0);        // 8 bits per transfer, SPI mode 0
    spi.frequency(1000000); // 1 MHz (safe value)

    cs = 1; // deselect ADC

    printf("GSR Click SPI test\r\n");

    while (true) {

        cs = 0;                     // select ADC
        rx[0] = spi.write(0x00);    // clock out first byte
        rx[1] = spi.write(0x00);    // clock out second byte
        cs = 1;                     // deselect ADC

        // MCP3201 data extraction
        adc_value = ((rx[0] & 0x1F) << 7) | (rx[1] >> 1);

        printf("Raw ADC: %u\r\n", adc_value);

        ThisThread::sleep_for(500ms);
    }
}
