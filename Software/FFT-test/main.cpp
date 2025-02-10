#include "mbed.h"
#include <cstdint>
#include <iostream>
#include <complex>
#include <cmath>
#include <chrono>

// Implementation of the Constant Geometry FFT algorithm
// by YEG

// parameters
#define test_frequency      1000            // a test frequency used to ensure FFT works as intended
#define piping_frequency    1200            // the frequency of queen piping
#define detection_threshold 1000            // the magnitude required to be classified as "detected" (100% volume monitor speakers)

// IO
AnalogIn mic(PA_3);             // mic/acc input
DigitalOut samp_pin(PC_0);      // test output pin
DigitalOut red(PB_14);          // red LED
DigitalOut green(PB_0);         // green LED
DigitalOut blue(PB_7);          // blue LED

// Timers
Ticker t;       // sampling timer
Timer tmr_p;    // timer used for piping


// timing
constexpr uint16_t fs = 8192;                           // sampling frequency
constexpr std::chrono::microseconds Ts(1000000/fs);     // sampling time period (us)



//////////////////////////////////// FFT STUFF ////////////////////////////////////

using std::complex;
using std::pow;


constexpr float pi = 3.14159;
constexpr uint16_t N = 1<<9;   // ensures it is a power of 2

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

///////////////////////////////////////////////////////////////////////////////////

//////////////////// PIPING STUFF ////////////////////

// piping counters
uint8_t cnt_long_pulse;         // determines how long the "long piping pulse" is
uint8_t cnt_short_pulse;        // counts number of shorter piping pulses
// piping flags
bool long_pulse;                // was a long pulse detected?
bool piping_detected;           // was piping detected?

//////////////////////////////////////////////////////

////////// DEBUG STUFF //////////

// #define DEBUG    // debug prints on/off
// #define TUNING   // tuning prints on/off
Timer tmr;          // loop timer

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

        #ifdef DEBUG 
            tmr.start();
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

        #ifdef DEBUG
            tmr.stop();
            std::cout << "Loop time (ms): " << tmr.elapsed_time().count()/1000 << std::endl;
            tmr.reset();

            // print out test frequency
            // if(!mode) std::cout << test_frequency << " Hz: " << abs(x_2[k_test]) << std::endl;
            // else std::cout << test_frequency << " Hz: " << abs(x_1[k_test]) << std::endl;
        #endif

        if(!mode) {
            mag2 = mag1;            // old sample
            mag1 = abs(x_2[k_p]);   // new sample
        }
        else {
            mag2 = mag1;
            mag1 = abs(x_1[k_p]);
        }

        // moving average (2 samples)
        mag_avg = (mag1+mag2)/2;

        // simple test for tuning
        // to determine detection_threshold
        #ifdef TUNING
            if(mag_avg > 1000) {
                green = 1;
                std::cout << mag_avg << std::endl;
            }
            else green= 0;
        #endif



        // piping detection //

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
                    tmr_p.start();  // start 1 second window
                    cnt_long_pulse++;   // increment when f is detected
                }
                // when window elapses, determine whether it was the long piping pulse or not
                if(tmr_p.elapsed_time().count()/1000 > 1000) {
                    tmr_p.stop();
                    tmr_p.reset();
                    
                    if(cnt_long_pulse >= 12) {
                        long_pulse = 1;    // long pulse has been detected
                        cnt_long_pulse = 0; // reset counter
                        red = 1;
                    }
                    cnt_long_pulse = 0; // reset counter
                }

            }

            // 2 - detecting the shorter pulses
            // in a window of 5 seconds
            // from different audio recordings, about 5 or 6 pulses occur within this time frame

            else {

                // first, detect the f
                if(mag_avg > detection_threshold) {
                    tmr_p.start();      // start 5 second window
                    cnt_short_pulse++;   // increment when f is detected
                }
                // when window elapses, determine whether it was the long piping pulse or not
                if(tmr_p.elapsed_time().count()/1000 > 5000) {
                    tmr_p.stop();
                    tmr_p.reset();
                    
                    if(cnt_short_pulse >= 6) {
                        piping_detected = 1;    // piping is now detected
                        cnt_short_pulse = 0;    // reset counter
                    }
                    cnt_short_pulse = 0;    // reset counter
                }


            }

        }

        // if piping is detected
        // do something
        else {

            blue = !blue;
            red = !red;
            
        }


        // TODO:
        // automatically determine loop time once at the start of the program
        // something to disable the piping_detected flag (timer/manually)
        // deal with following condition: detected long pulse, but no short pulse; reset

        

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
