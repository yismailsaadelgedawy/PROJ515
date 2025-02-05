#include <iostream>
#include <complex>
#include <cmath>
#include <vector>
#include "mbed.h"

using namespace std;

constexpr double PI = 3.14159265358979323846;
constexpr uint16_t N = 1 << 10; // Must be a power of 2

vector<complex<double>> W_array(N / 2);
vector<complex<double>> x(N); // Input signal
vector<complex<double>> X(N); // Output signal (frequency domain)

// Function to compute bit-reversed index
uint32_t bit_reverse(uint32_t num, int numBits) {
    uint32_t reversed = 0;
    for (int i = 0; i < numBits; i++) {
        reversed |= ((num >> i) & 1) << (numBits - 1 - i);
    }
    return reversed;
}

// Precompute twiddle factors
void compute_twiddle_factors() {
    for (int i = 0; i < N / 2; i++) {
        W_array[i] = polar(1.0, -2.0 * PI * i / N);
    }
}

// Perform iterative in-place FFT using the Cooley-Tukey algorithm
void fft(vector<complex<double>>& data) {
    int logN = log2(N);
    
    // Bit-reverse copy
    vector<complex<double>> temp(N);
    for (int i = 0; i < N; i++) {
        temp[bit_reverse(i, logN)] = data[i];
    }
    data = move(temp);
    
    // Iterative FFT
    for (int s = 1; s <= logN; s++) {
        int m = 1 << s;  // 2^s
        int half_m = m / 2;
        complex<double> wm;
        for (int k = 0; k < N; k += m) {
            for (int j = 0; j < half_m; j++) {
                wm = W_array[j * (N / m)];
                complex<double> t = wm * data[k + j + half_m];
                complex<double> u = data[k + j];
                data[k + j] = u + t;
                data[k + j + half_m] = u - t;
            }
        }
    }
}


// MCU stuff //

// IO
AnalogIn mic(PA_3); // mic/acc input
DigitalOut samp_pin(PC_0); // test output pin

// sampling timer
Ticker t;

// timing
constexpr uint16_t fs = 8192;   // sampling frequency
constexpr std::chrono::microseconds Ts(1000000/fs); // sampling time period (us)

// ISRs
void sampling_ISR();

int main() {

    // precompute them once
    compute_twiddle_factors();

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

        
        fft(x);

        std::cout << "1000Hz: " << abs(x[125]) << std::endl;
        
        // // Print FFT magnitude results
        // for (const auto& val : x) {
        //     cout << abs(val) << endl;
        // }

    }
}


// sampling ISR
void sampling_ISR() {

    samp_pin = !samp_pin;   // toggle test pin (for probing)

}
