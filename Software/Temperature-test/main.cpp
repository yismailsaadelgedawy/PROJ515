#include "mbed.h"
#include "stdint.h"
#include <cstdint>
#include <iostream>

// general parameters
#define samples 4                       // number of samples for moving average
#define lower_temp_th 30.0f             // lower threshold for brood temp range (34.5f actual)
#define upper_temp_th 35.5f             // upper threshold for brood temp range
#define total_number_of_sensors 4       // total number of sensors used in array
#define addr0 PD_6
#define addr1 PD_7

// IO
DigitalOut red(PB_14);                      // red LED
DigitalOut green(PB_0);                     // green LED
BusOut addr(addr0, addr1);                  // LSB -> MSB
AnalogIn temp_sense(PA_3);


// vars
float temp[samples];
uint8_t temp_idx;
float temp_avg;

bool sensors_in_brood_area[total_number_of_sensors];
bool inBroodArea;
uint8_t sum;


// functions
bool sense(int sensor_number);

// debug switches
// #define DEBUG   // toggles debug prints

int main()
{

    // address = 0 initially -> first sensor selected
    addr = 0;

    while (true) {

        // determine the sensors within the brood area
        if( sense(addr) ) sensors_in_brood_area[addr] = 1;
        else sensors_in_brood_area[addr] = 0;

        // print out the number of sensors within the brood area
        for(int i=0; i<total_number_of_sensors; i++) sum += sensors_in_brood_area[i];

        #ifndef DEBUG
            std::cout << "Number of sensors in brood area: " << (int)sum << std::endl;
        #endif

        sum = 0;            // reset sum
        addr = addr + 1;    // select next sensor
        

        wait_us(500000);    // wait a bit

    }
}

// temperature measurement function
bool sense(int sensor_number) {

    while(1) {

        // take raw reading
        temp[temp_idx] = (temp_sense.read() * 3.3f) / 0.0825f;  // 82.5mV/deg C
        temp_idx++;

        // once buffer is filled
        if(temp_idx == samples) {

            // reset index
            temp_idx = 0;
            // sum var
            float sum = 0;

            // calculate moving average
            for(int i=0; i<samples; i++) sum += temp[i];
            temp_avg = sum/samples;

            break;
            
        }

    }

    #ifdef DEBUG

        std::cout << "Sensor " << sensor_number + 1 << ": " << temp_avg << std::endl;

    #endif

    
    // brood temp range check
    if(temp_avg > lower_temp_th && temp_avg < upper_temp_th) inBroodArea = 1;
    else inBroodArea = 0;
    
    

    return inBroodArea;
}



