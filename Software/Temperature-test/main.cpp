#include "mbed.h"
#include "stdint.h"
#include <cstdint>
#include <iostream>

// general parameters
#define samples 4                       // number of samples for moving average
#define lower_temp_th 34.5f             // lower threshold for brood temp range
#define upper_temp_th 35.5f             // upper threshold for brood temp range
#define total_number_of_sensors 1       // total number of sensors used in array


// debug stuff
// #define DEBUG       // debug prints on/off


// temperature sensor class
template<uint8_t N>
class Temperature {

    private:
    mbed::AnalogIn temp_sensor;
    static uint8_t sensor_cnt;  // static: persists; class var not an object var
    uint8_t sensor_number;
    float temp[N];
    float temp_avg;
    uint8_t temp_idx;
    bool inBroodArea;

    public:
    Temperature(mbed::AnalogIn pin_name) : temp_sensor(pin_name) {

        // keeps a count of how many sensors there are
        this->sensor_cnt++;

        // assign it a number
        this->sensor_number = this->sensor_cnt;
    }


    // temperature measurement function
    bool sense() {

        // take raw reading
        this->temp[this->temp_idx] = this->temp_sensor.read() * 50.0f; // scaled to mimic temp readings
        this->temp_idx++;

        // once buffer is filled
        if(temp_idx == N) {

            // reset index
            this->temp_idx = 0;
            // sum var
            float sum = 0;

            // calculate moving average
            for(int i=0; i<N; i++) sum += temp[i];
            this->temp_avg = sum/N;

            
        }

        #ifdef DEBUG
            std::cout << "Sensor " << (int)this->sensor_number << ": " << this->temp_avg << std::endl;
        #endif

        // brood temp range check
        if(this->temp_avg > lower_temp_th && this->temp_avg < upper_temp_th) this->inBroodArea = 1;
        else this->inBroodArea = 0;
        
        

        return inBroodArea;

    }


};

// must init statics outside of class definition
template<uint8_t N>
uint8_t Temperature<N>::sensor_cnt = 0;



// IO
DigitalOut red(PB_14);                      // red LED
DigitalOut green(PB_0);                     // green LED

// Array of temperature sensors
Temperature<samples> temp_sensor_1(PA_0);   // pot mimicking temp sensor

// vars
bool sensors_in_brood_area[total_number_of_sensors];
uint8_t sum;


int main()
{
    while (true) {

        // determine the sensors within the brood area
        if(temp_sensor_1.sense()) sensors_in_brood_area[0] = 1;
        else sensors_in_brood_area[0] = 0;

        // print out the number of sensors within the brood area
        for(int i=0; i<total_number_of_sensors; i++) sum += sensors_in_brood_area[i];


        std::cout << "Number of sensors in brood area: " << (int)sum << std::endl;
        sum = 0;        // reset sum
        

        wait_us(50000); // 50ms

    }
}



