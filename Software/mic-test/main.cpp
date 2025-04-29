#include "mbed.h"
#include <cstdint>
#include <iostream>
#include <complex>
#include <cmath>
#include <chrono>

// Implementation of the Constant Geometry FFT algorithm
// by YEG

// general parameters
#define test_frequency          240             // a test frequency used to ensure FFT works as intended
#define predator_frequency      240             // the frequency of predator
#define detection_threshold     1000            // the magnitude required to be classified as "ON" (100% volume monitor speakers)
#define off_threshold           800             // the magnitude required to be classified as "OFF" (100% volume monitor speakers)

// IO
AnalogIn mic(PC_3);             // micR input
DigitalOut samp_pin(PA_3);      // test output pin
DigitalOut red(PB_14);          // red LED
DigitalOut green(PB_0);         // green LED
DigitalOut blue(PB_7);          // blue LED

// Int pins
InterruptIn hornet_pin(PA_6);

// SPI

// Timers
Timer tmr;          // general timer
Ticker t;           // sampling timer
Timer tmr_pred;     // timer used for predator detection
Timer tmr_fft;      // tuner timer used to run fft for a while after trigger

// timing
constexpr uint16_t fs = 8192;                           // sampling frequency
constexpr std::chrono::microseconds Ts(1000000/fs);     // sampling time period (us)
uint8_t loop_time_state;                                // used to control loop time calculation



//////////////////////////////////// FFT STUFF ////////////////////////////////////

using std::complex;
using std::pow;


constexpr float pi = 3.14159;   // pi :)
constexpr uint16_t N = 1<<9;    // ensures it is a power of 2

constexpr double f_res = (1.0f)/(N * 1.0f/fs);                          // frequency resolution (currently it is 8Hz - good enough; saves memory)
constexpr uint16_t k_test = test_frequency/(uint16_t)f_res;             // obtains the frequency bin, k, corresponding to test_frequency
constexpr uint16_t k_pred = predator_frequency/(uint16_t)f_res;         // obtains the frequency bin, k, corresponding to piping_frequency


constexpr complex<double> z3(0,-2*pi/N);    // -2*pi*j/N
const complex<double> w = exp(z3);          // twiddle factor, w = e^(-2*pi*j/N) - (exp isn't a constexpr)

double x[N];                    // input time domain
complex<float> x_1[N];          // array 1 - time domain (scrambled)
complex<float> x_2[N];          // array 2 - time domain; two arrays due to constant geometry
complex<float> W_array[N/2];    // twiddle factors (precomputed)
complex<float> mul;             // holds the result of the W*B product; optimises 2 multiplications down to 1 
uint16_t mag1,mag2,mag_avg;     // 2 magnitude samples (n and n-1), and an average of them
bool avg = 1;                   // controls whether moving average is enabled or not; enabled by default

constexpr uint32_t fft_time_us = 5000000;   // sets how long the FFT to run for after the trigger (5 seconds by default)


///////////////////////////////////////////////////////////////////////////////////

//////////////////// PREDATOR STUFF ////////////////////

// predator counters
uint8_t cnt_long_pulse;         // determines how long the "long predator pulse" is
// predator flags
bool pred_detected;             // was a predator detected?
// variables
uint8_t long_samples_expected;
// constexpr uint8_t short_samples_expected = 6;
// parameters
constexpr uint16_t long_pulse_duration_ms = 800;

//////////////////////////////////////////////////////

////////// DEBUG STUFF //////////

// #define DEBUG                // debug prints on/off
// #define TUNING               // tuning prints on/off
#define PREDATOR             // predator algorithm on/off
#define PREDATOR_DEBUG       // predator debug prints on/off
#define FFT_TIMING           // timed FFT on/off

/////////////////////////////////


// functions
uint32_t bit_reverse(uint32_t num, int numBits);    // bit scrambling algorithm

// ISRs
void sampling_ISR();                                // sampling interrupt service routine
void trigger_filter_hornet_ISR();                          // trigger filter interrupt service routine from PCB


int main() {

    #ifdef FFT_TIMING
        // setup the trigger ISR
        hornet_pin.rise(trigger_filter_hornet_ISR);
        // wait for trigger pin
        sleep();
    #endif

    // precompute twiddle factors - ONCE
    // optimises for speed
    for (int i=0; i<(N/2); i++) {
        
        W_array[i] = pow(w,i);          // W^0, W^1, ... W^(N/2 - 1)
    }

    while(1) {

        #ifdef FFT_TIMING
            // after triggering, run FFT for a bit
            tmr_fft.start();

            if(tmr_fft.elapsed_time().count() > fft_time_us) {

                tmr_fft.stop();                         // stop timer
                tmr_fft.reset();                        // reset timer
                t.detach();                             // disable fft
                hornet_pin.rise(trigger_filter_hornet_ISR);    // re-enable the trigger interrupt
                sleep();                                // wait for trigger...
                tmr_fft.start();                        // start timer again
                
            }
        #endif

        
        t.attach(sampling_ISR, Ts);     // setup sampling ISR
        sleep();                        // wait for ISR

        // start reading values...
        for(int n=0; n<N; n++) {
            x[n] = (mic.read() - 0.5) * 100.0f;     // 0.5 to remove DC offset; scaled to make numbers nicer to work with
            sleep();                                // will only wake up by sampling ISR
        }

        t.detach(); // stop reading
        
        ///////////////////////////// FFT /////////////////////////////

        // 1 - scramble data
        for (int i=0; i<N; i++) {
            
            int i_scrambled = bit_reverse(i,log2(N));
            x_1[i] = x[i_scrambled];
        }

        // 2 - perform the FFT
        // s: current stage (0 indexed)
        // n: current butterfly (0 indexed)
        bool mode = 1;                          // determines polarity of x1 and x2 arrays
        uint16_t w_idx = 0;                     // twiddle factor index
        uint16_t repetitions = (2*N)/8;         // number of repeats/holds
        uint16_t rep_cnt = 0;                   // counter to implement the repeats
        for (int s=0; s<log2(N); s++) {

            // only from third stage onwards
            if(s > 0 && s > 1) repetitions = repetitions >> 1; // r = r/2; compiler will probably optimise into an LSR, but better to be safe

            // when done:
            mode = !mode;   // switch the polarities of the x1 and x2 arrays;
            rep_cnt = 0;    // reset the counter;
            w_idx = 0;      // reset index

            // calculate the step size of current stage
            uint16_t step = N/( 1<<(s+1) );
            
            // iterates over stages
            for (int n=0; n<(N/2); n++) {

                // only on first and last stage
                if(s==0 || s == log2(N)-1) {
                    // induce overflow
                    // prevents out of bounds idx
                    w_idx = (n*step > (N/2)-1) ? 0 : (n*step);
                }

                else {

                    rep_cnt++;
                    if(rep_cnt > repetitions) {

                        w_idx += step;  // when done repeating, go to next twiddle index
                        rep_cnt = 1;    // reset the counter

                    }
                }
                

                // this MUST be done first
                if(!mode) {
                    mul = W_array[w_idx]*( x_1[(2*n)+1] );  // compute the product only once

                    x_2[n] = x_1[2*n] + mul;               
                    x_2[(N/2)+n] = x_1[2*n] - mul;
                }
                else {
                    mul = W_array[w_idx]*( x_2[(2*n)+1] );  // compute the product only once

                    x_1[n] = x_2[2*n] + mul;               
                    x_1[(N/2)+n] = x_2[2*n] - mul;
                }     

            }
            
            
        }

        ///////////////////////////////////////////////////////////////

        // print out test frequency
        #ifdef DEBUG
            if(!mode) std::cout << test_frequency << " Hz: " << abs(x_2[k_test]) << std::endl;
            else std::cout << test_frequency << " Hz: " << abs(x_1[k_test]) << std::endl;
        #endif

        // polarity depends on mode remember; choose the output array accordingly
        if(!mode) {
            mag2 = mag1;            // old sample (n-1)
            mag1 = abs(x_2[k_pred]);   // new sample (n)
        }
        else {
            mag2 = mag1;
            mag1 = abs(x_1[k_pred]);
        }

        // calculation of loop time (end)
        // and print loop time
        if(loop_time_state == 1) {
            tmr.stop();
            uint16_t loop_time_ms = tmr.elapsed_time().count()/1000;
            std::cout << "Loop time (ms): " << loop_time_ms << std::endl;
            tmr.reset();
            loop_time_state = 2;

            // calculate the number of expected long predator samples
            // within a 1sec time window; function of loop time!
            long_samples_expected = long_pulse_duration_ms/loop_time_ms;
        }

        // --- DO NOT PUT CODE HERE --- //

        // may enable/disable moving average (2 samples) at will
        if(avg) mag_avg = (mag1+mag2)/2;
        else mag_avg = mag1;

        // --- DO NOT PUT CODE HERE --- //

        // calculation of loop time (start)
        // how long it takes to obtain a magnitude in the frequency domain
        if(loop_time_state == 0) {
            tmr.start();
            loop_time_state = 1;
        }
        
        
        // simple test for tuning
        // to determine detection_threshold
        #ifdef TUNING
            if(mag_avg > detection_threshold) {
                green = 1;
                std::cout << mag_avg << std::endl;
            }
            else green = 0;
        #endif


        // predator detection
        #ifdef PREDATOR

            // if predator isn't detected
            // run the algorithm
            if(!pred_detected) {
            
                // 1 - detecting the initial long pulse
                // the pulse lasts about 1sec
                // loop time is measured to be 87ms 
                // so 1sec of a continous f, results in 11.5 samples
                
                

                // first, detect the f
                if(mag_avg > detection_threshold) {
                    tmr_pred.start();      // start 1 second window
                    cnt_long_pulse++;   // increment when f is detected
                }
                // when window elapses, determine whether it was the long predator pulse or not
                if(tmr_pred.elapsed_time().count()/1000 > long_pulse_duration_ms) {
                    tmr_pred.stop();
                    tmr_pred.reset();

                    #ifdef PREDATOR_DEBUG
                        std::cout << "long: " << (int)cnt_long_pulse << std::endl;
                    #endif

                    if(cnt_long_pulse >= long_samples_expected) {
                        cnt_long_pulse = 0; // reset counter
                        pred_detected = 1;
                    }
                    cnt_long_pulse = 0;     // reset counter
                }

                

            }

            // if predator is detected
            // do something
            else {

                blue = !blue;
                red = !red;
                
            }

        #endif


        // TODO:
        // something to disable the pred_detected flag (timer/manually) -- later
        

    }
    
   
}

// sampling ISR
void sampling_ISR() {

    samp_pin = !samp_pin;   // toggle test pin (for probing)

}

void trigger_filter_hornet_ISR() {

    hornet_pin.rise(NULL);  // detach to avoid queueing
    // red = 1;             // use this to debug

}


// Function to reverse bits in an unsigned integer of 'numBits' size
uint32_t bit_reverse(uint32_t num, int numBits) {
    uint32_t reversed = 0;
    
    for (int i = 0; i < numBits; i++) {
        // Extract the least significant bit
        uint32_t bit = (num >> i) & 1;
        
        // Place it in the reversed position
        reversed |= (bit << (numBits - 1 - i));
    }
    
    return reversed;

}





