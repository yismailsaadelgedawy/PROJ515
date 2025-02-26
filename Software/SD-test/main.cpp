#include "mbed.h"
#include <iostream>
#include "stdint.h"
#include "SDcard.hpp"
#include <string_view>

#define SD_detect_pin PF_4
#define fifo_depth 10

DigitalIn SDmountCheck(SD_detect_pin);

CircularBuffer<int, fifo_depth> fifo_mag;
CircularBuffer<char*, fifo_depth> fifo_time;

int rx_mag;             // received payloads
char* rx_time;          //

// main() runs in its own thread in the OS
int main()
{

    char* s = "Wednesday, 26 February 2025";

    int dummy_data = 0;

    // You should check if the buffer is full before pushing the data because a full buffer overwrites the data.
    // both buffers are the same size

    // pushing data onto buffer
    while (!fifo_mag.full()) {
        fifo_mag.push(dummy_data++);    // store the magnitude
        fifo_time.push(s);              // store the date and time
    }


    

    // popping data off buffer
    while (!fifo_mag.empty()) {

        fifo_mag.pop(rx_mag);
        fifo_time.pop(rx_time);

        // std::cout << rx_mag << std::endl;
        // std::cout << rx_time << std::endl;


        mywrite_sdcard(rx_mag, rx_time);    // store values in SD card
    }


    std::cout << "done" << std::endl;

    
    return 0;   // exit

}

