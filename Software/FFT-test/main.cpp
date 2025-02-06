#include "mbed.h"
#include <cstdint>
#include <iostream>
#include <complex>
#include <cmath>
#include <chrono>

#define test_frequency 500

// IO
AnalogIn mic(PA_3); // mic/acc input
DigitalOut samp_pin(PC_0); // test output pin

// sampling timer
Ticker t;

// timing
constexpr uint16_t fs = 8192;   // sampling frequency
constexpr std::chrono::microseconds Ts(1000000/fs); // sampling time period (us)



//////////////////////////////////// FFT STUFF ////////////////////////////////////

using std::complex;
using std::pow;


constexpr float pi = 3.14159;
constexpr uint16_t N = 1<<10; // ensures it is a power of 2

constexpr double f_res = (1.0f)/(N * 1.0f/fs);  // frequency resolution (currently it is 8Hz - good enough; saves memory)


complex<double> z3(0,-2*pi/N); // -2*pi*j/N
complex<double> w = exp(z3);    // twiddle factor, w = e^(-2*pi*j/N)

double x[N]; // input time domain
complex<float> x_1[N]; // array 1 - time domain (scrambled) -- temporary, will optimise out later!
complex<float> x_2[N]; // array 2 - time domain
complex<float> W_array[N/2];    // twiddle factors (precomputed)

///////////////////////////////////////////////////////////////////////////////////

// functions
uint32_t bit_reverse(uint32_t num, int numBits);

// ISRs
void sampling_ISR();


int main() {

    // precompute twiddle factors
    for (int i=0; i<(N/2); i++) {
        
        W_array[i] = pow(w,i);  // W^0, W^1, ... W^(N/2 - 1)
    }

    while(1) {

        // setup sampling ISR
        t.attach(sampling_ISR, Ts);
        sleep();    // wait for ISR

        // start reading values...
        
        for(int n=0; n<N; n++) {
            x[n] = (mic.read() - 0.5) * 100.0f; // 0.5 to remove DC offset; scaled to make numbers nicer to work with
            sleep();    // will only wake up by sampling ISR
        }

        t.detach(); // stop reading
        
        /////////////////////////////FFT/////////////////////////////

        // 1 - scramble data
        for (int i=0; i<N; i++) {
            
            int i_scrambled = bit_reverse(i,log2(N));
            x_1[i] = x[i_scrambled];
        }

        

        // 2 - perform the FFT
        // s: current stage (0 indexed)
        // n: current butterfly (0 indexed)
        bool mode = 1;
        uint16_t w_idx = 0;
        uint16_t repetitions = (2*N)/8; // number of repeats/holds
        uint16_t rep_cnt = 0;
        for (int s=0; s<log2(N); s++) {

            if(s > 0 && s > 1) repetitions = repetitions/2; // r = r/(2^s-1)

            // when done, switch the polarities of the x1 and x2 arrays
            mode = !mode;
            rep_cnt = 0;    // and reset the counter as well
            w_idx = 0;      // reset index

            // calculate the step size of current stage
            uint16_t step = N/( 1<<(s+1) );
            
            for (int n=0; n<(N/2); n++) {

                if(s==0 || s == log2(N)-1) {
                    // induce overflow
                    // prevents out of bounds idx
                    w_idx = (n*step > (N/2)-1) ? 0 : (n*step);  // warning: reinit overhead!
                }

                else {

                    rep_cnt++;
                    if(rep_cnt > repetitions) {

                        // induce overflow
                        // prevents out of bounds idx
                        // w_idx = (n*step/repetitions > (N/2)-1) ? 0 : (n*step/repetitions);  // warning: reinit overhead!
                        w_idx += step;

                        rep_cnt = 1; // reset the counter

                    }
                }
                

                // this MUST be done first
                if(!mode) {
                    x_2[n] = x_1[2*n] + W_array[w_idx]*( x_1[(2*n)+1] );               
                    x_2[(N/2)+n] = x_1[2*n] - W_array[w_idx]*( x_1[(2*n)+1] );
                }
                else {
                    x_1[n] = x_2[2*n] + W_array[w_idx]*( x_2[(2*n)+1]) ;               
                    x_1[(N/2)+n] = x_2[2*n] - W_array[w_idx]*( x_2[(2*n)+1] );
                }       

            }
            
            
        }


        /////////////////////////////////////////////////////////////

        // print out test frequency

        if(!mode) std::cout << test_frequency << " Hz: " << abs(x_2[test_frequency/(uint16_t)f_res]) << std::endl;
        else std::cout << test_frequency << " Hz: " << abs(x_1[test_frequency/(uint16_t)f_res]) << std::endl;
        

        

        

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