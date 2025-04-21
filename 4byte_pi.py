import spidev
import time
time.sleep(2)

spi = spidev.SpiDev()
spi.open(0, 0)  #bus 0 device 0
spi.bits_per_word = 8
spi.max_speed_hz = 500000
spi.mode = 0  

while True:
    time.sleep(0.5)
    #sends mcu dummy bytes
    response = spi.xfer2([0x00, 0x00, 0x00, 0x00])
    print("Received:", response)
    #delay if needed
    #time.sleep(0.5)
