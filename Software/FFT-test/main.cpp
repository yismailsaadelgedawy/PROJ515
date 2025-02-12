#include "mbed.h"
#include <cstdint>
#include <iostream>
#include <complex>
#include <cmath>
#include <chrono>

// Implementation of the Constant Geometry FFT algorithm
// by YEG

// general parameters
#define test_frequency      1200            // a test frequency used to ensure FFT works as intended
#define piping_frequency    1200            // the frequency of queen piping
#define detection_threshold 1400            // the magnitude required to be classified as "ON" (100% volume monitor speakers)
#define off_threshold       1000            // the magnitude required to be classified as "OFF" (100% volume monitor speakers)

// IO
AnalogIn mic(PA_3);             // mic/acc input
DigitalOut samp_pin(PC_0);      // test output pin
DigitalOut red(PB_14);          // red LED
DigitalOut green(PB_0);         // green LED
DigitalOut blue(PB_7);          // blue LED

// Timers
Timer tmr;      // general timer
Ticker t;       // sampling timer
Timer tmr_p;    // timer used for piping

// timing
constexpr uint16_t fs = 8192;                           // sampling frequency
constexpr std::chrono::microseconds Ts(1000000/fs);     // sampling time period (us)
uint8_t loop_time_state;                                // used to control loop time calculation



//////////////////////////////////// FFT STUFF ////////////////////////////////////

using std::complex;
using std::pow;


constexpr float pi = 3.14159;   // pi :)
constexpr uint16_t N = 1<<9;    // ensures it is a power of 2

constexpr double f_res = (1.0f)/(N * 1.0f/fs);                  // frequency resolution (currently it is 8Hz - good enough; saves memory)
constexpr uint16_t k_test = test_frequency/(uint16_t)f_res;     // obtains the frequency bin, k, corresponding to test_frequency
constexpr uint16_t k_p = piping_frequency/(uint16_t)f_res;      // obtains the frequency bin, k, corresponding to piping_frequency


constexpr complex<double> z3(0,-2*pi/N);    // -2*pi*j/N
const complex<double> w = exp(z3);          // twiddle factor, w = e^(-2*pi*j/N) - (exp isn't a constexpr)

double x[N];                    // input time domain
complex<float> x_1[N];          // array 1 - time domain (scrambled)
complex<float> x_2[N];          // array 2 - time domain; two arrays due to constant geometry
complex<float> W_array[N/2];    // twiddle factors (precomputed)
complex<float> mul;             // holds the result of the W*B product; optimises 2 multiplications down to 1 
uint16_t mag1,mag2,mag_avg;     // 2 magnitude samples (n and n-1), and an average of them
bool avg = 1;                   // controls whether moving average is enabled or not; enabled by default

///////////////////////////////////////////////////////////////////////////////////

//////////////////// PIPING STUFF ////////////////////

// piping counters
uint8_t cnt_long_pulse;         // determines how long the "long piping pulse" is
uint8_t cnt_short_pulse;        // counts number of shorter piping pulses
// piping flags
bool long_pulse;                // was a long pulse detected?
bool piping_detected;           // was piping detected?
// variables
uint8_t long_samples_expected;
constexpr uint8_t short_samples_expected = 6;
// parameters
constexpr uint16_t long_pulse_duration_ms = 900;
constexpr uint16_t short_pulses_duration_ms = 5000;

//////////////////////////////////////////////////////

////////// DEBUG STUFF //////////

// #define DEBUG            // debug prints on/off
// #define TUNING           // tuning prints on/off
#define PIPING           // piping algorithm on/off
#define PIPING_DEBUG     // piping debug prints on/off

/////////////////////////////////


// functions
uint32_t bit_reverse(uint32_t num, int numBits);    // bit scrambling algorithm

// ISRs
void sampling_ISR();                                // sampling interrupt service routine


int main() {

    // precompute twiddle factors - ONCE
    // optimises for speed
    for (int i=0; i<(N/2); i++) {
        
        W_array[i] = pow(w,i);          // W^0, W^1, ... W^(N/2 - 1)
    }

    while(1) {
        
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
            mag1 = abs(x_2[k_p]);   // new sample (n)
        }
        else {
            mag2 = mag1;
            mag1 = abs(x_1[k_p]);
        }

        // calculation of loop time (end)
        // and print loop time
        if(loop_time_state == 1) {
            tmr.stop();
            uint16_t loop_time_ms = tmr.elapsed_time().count()/1000;
            std::cout << "Loop time (ms): " << loop_time_ms << std::endl;
            tmr.reset();
            loop_time_state = 2;

            // calculate the number of expected long piping samples
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


        // piping detection
        #ifdef PIPING

            // if piping isn't detected
            // run the algorithm
            if(!piping_detected) {
            
                // 1 - detecting the initial long pulse
                // the pulse lasts about 1sec
                // loop time is measured to be 87ms 
                // so 1sec of a continous f, results in 11.5 samples
                
                if(!long_pulse) {

                    // first, detect the f
                    if(mag_avg > detection_threshold) {
                        tmr_p.start();      // start 1 second window
                        cnt_long_pulse++;   // increment when f is detected
                    }
                    // when window elapses, determine whether it was the long piping pulse or not
                    if(tmr_p.elapsed_time().count()/1000 > long_pulse_duration_ms) {
                        tmr_p.stop();
                        tmr_p.reset();

                        #ifdef PIPING_DEBUG
                            std::cout << "long: " << (int)cnt_long_pulse << std::endl;
                        #endif

                        if(cnt_long_pulse >= long_samples_expected) {
                            long_pulse = 1;     // long pulse has been detected
                            cnt_long_pulse = 0; // reset counter
                            red = 1;
                        }
                        cnt_long_pulse = 0;     // reset counter
                    }

                }

                // 2 - detecting the shorter pulses
                // in a window of 5 seconds
                // from different audio recordings, about 5 or 6 pulses occur within this time frame

                else {

                    tmr_p.start();      // start 5 second window straight away
                    avg = 0;            // disable averaging; faster response; more appropriate for the short pulses we are looking for

                    // then, detect the f
                    // pulse detection
                    // rising edge detector (think Verilog)
                    if(mag1 > detection_threshold && mag2 < off_threshold) cnt_short_pulse++;
                    
                    // when window elapses, determine whether it was the long piping pulse or not
                    if(tmr_p.elapsed_time().count()/1000 > short_pulses_duration_ms) {
                        tmr_p.stop();
                        tmr_p.reset();

                        avg = 1;    // re-enable averaging

                        #ifdef PIPING_DEBUG
                            std::cout << "short: " << (int)cnt_short_pulse << std::endl;
                        #endif

                        if(cnt_short_pulse >= short_samples_expected) {
                            piping_detected = 1;    // piping is now detected
                            cnt_short_pulse = 0;    // reset counter
                        }
                        else {
                            long_pulse = 0;         // reset flag; restart the process; it wasn't piping
                            red = 0;                // reset indicator LED
                        }   
                        cnt_short_pulse = 0;        // reset counter
                    }


                }

            }

            // if piping is detected
            // do something
            else {

                blue = !blue;
                red = !red;
                
            }

        #endif


        // TODO:
        // something to disable the piping_detected flag (timer/manually) -- later
        

    }
    
   
}

// sampling ISR
void sampling_ISR() {

    samp_pin = !samp_pin;   // toggle test pin (for probing)

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



