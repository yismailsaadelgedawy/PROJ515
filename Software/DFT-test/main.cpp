#include "mbed.h"
#include <cstdint>
#include <iostream>
#include <complex>
#include <cmath>
#include <chrono>

// #define DEBUG

// IO
AnalogIn mic(PA_3); // mic/acc input
DigitalOut samp_pin(PC_0); // test output pin

// sampling timer
Ticker t;

// timing
constexpr uint16_t fs = 8192;   // sampling frequency
constexpr std::chrono::microseconds Ts(1000000/fs); // sampling time period (us)


// flags


//////////////////////////////////// DFT STUFF ////////////////////////////////////


using std::complex;
using std::pow;


constexpr float pi = 3.14159;
constexpr uint16_t N = 1<<5; // ensures it is a power of 2

constexpr double f_res = (1.0f)/(N * 1.0f/fs);  // frequency resolution


complex<double> z3(0,-2*pi/N); // -2*pi*j/N
complex<double> w = exp(z3);    // twiddle factor, w = e^(-2*pi*j/N)

// symmetry across k=N/2
// double the speed!
// half the memory usage!
constexpr uint16_t symmetry_idx = (N/2)+1;

double x[N]; // input time domain
complex<double> X[symmetry_idx]; // output frequency domain; uninit globals and statics are zero
double X_mag[symmetry_idx]; // used for spectrum estimation

///////////////////////////////////////////////////////////////////////////////////

// functions

// ISRs
void sampling_ISR();

int main() {

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
        
        // then apply DFT
        for (int k=0; k<symmetry_idx; k++) {
            for(int n=0; n<symmetry_idx; n++) {

                X[k] += x[n] * pow(w,n*k);

            }
        }

        // obtain the magnitudes, for frequency spectrum
        for(int k=0; k<symmetry_idx; k++) X_mag[k] = abs(X[k]);

        #ifdef DEBUG
            for(int i=0; i<N; i++) std::cout << X[i] << std::endl;
            for(int i=0; i<N; i++) std::cout << i*f_res << ": " << X_mag[i] << std::endl;
        #endif

        // print out test frequency
        std::cout << 1000 << " Hz: " << X_mag[4] << std::endl;

        // clear X(k) array
        for (int k=0; k<symmetry_idx; k++) X[k] = 0;

        

    }

    
   
}

// sampling ISR
void sampling_ISR() {

    samp_pin = !samp_pin;   // toggle test pin (for probing)

}
