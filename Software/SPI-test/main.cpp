#include "mbed.h"

// Setup SPI Slave: mosi, miso, sclk, ssel
// PI SPI MUST be a master
// so PI must send dummy data to generate the clock
// so that the MCU can send its data
SPISlave spi4_slave(PE_14, PE_13, PE_12, PE_11); // MOSI, MISO, SCLK, CS


// SPI functions
void spi4_init();
void spi4_write(uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4);


SPI_HandleTypeDef hspi4;


int main() {

    spi4_init();

    while (true) {

        // continously calling SPI so mcu is always ready when pi talks
        spi4_write(0x01, 0x02, 0x03, 0x04);
       
            
    }
}


void spi4_init(void) {

    hspi4.Instance = SPI4;
    hspi4.Init.Mode = SPI_MODE_SLAVE;
    hspi4.Init.Direction = SPI_DIRECTION_2LINES;
    hspi4.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi4.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi4.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi4.Init.NSS = SPI_NSS_HARD_INPUT;
    hspi4.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi4.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi4.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi4.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&hspi4) != HAL_OK) printf("ERROR\n");
}

void spi4_write(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {

    uint8_t txBuf[5] = {a, b, c, d};
    uint8_t rxBuf[5] = {0, 0, 0, 0};

    if (HAL_SPI_TransmitReceive(&hspi4, txBuf, rxBuf, 4, HAL_MAX_DELAY) != HAL_OK) printf("SPI TRANSFER ERROR\n");

}
