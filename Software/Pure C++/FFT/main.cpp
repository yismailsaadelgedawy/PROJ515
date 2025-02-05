#include <iostream>
#include <complex>
#include <cmath>

using std::complex;
using std::pow;

constexpr float pi = 3.14159;
constexpr uint8_t N = 1<<4; // ensures it is a power of 2

complex<double> z3(0,-2*pi/N); // -2*pi*j/N
complex<double> w = exp(z3);    // twiddle factor, w = e^(-2*pi*j/N)


double x[N] = {1,0,0,0,1,1,1,0,0,0,0,1,1,1,0,0}; // input samples array
complex<double> x_1[N]; // array 1 - time domain (scrambled) -- temporary, will optimise out later!
complex<double> x_2[N]; // array 2 - time domain
// complex<double> X[N]; // output frequency domain; uninit globals and statics are zero
double X_mag[N]; // used for spectrum estimation

complex<double> W_array[N/2];
// uint8_t w_idx_stage_2 = ( (N)/(1<<(1+1)) ) * (1%(1<<1));        // ( (N)/(1<<(s+1)) ) * (n%(1<<s)), with n=s=1

// functions
uint32_t bit_reverse(uint32_t num, int numBits);


int main() {

    // 1 - scramble data
    for (int i=0; i<N; i++) {
        
        int i_scrambled = bit_reverse(i,log2(N));
        x_1[i] = x[i_scrambled];
    }

    // 2 - precompute twiddle factors
    for (int i=0; i<(N/2); i++) {
        
        W_array[i] = pow(w,i);  // W^0, W^1, ... W^(N/2 - 1)
    }

    // 3 - perform the FFT
    // s: current stage (0 indexed)
    // n: current butterfly (0 indexed)
    static bool mode = 1;   // initialises once (static local trick)
    uint8_t w_idx = 0;
    uint16_t repetitions = (2*N)/8; // number of repeats/holds
    uint16_t rep_cnt = 0;
    for (int s=0; s<log2(N); s++) {

        if(s > 0 && s > 1) repetitions = repetitions/(1<<(s-1)); // r = r/(2^s-1)

        // when done, switch the polarities of the x1 and x2 arrays
        mode = !mode;
        rep_cnt = 0;    // and reset the counter as well
        w_idx = 0;      // reset index

        // calculate the step size of current stage
        uint8_t step = N/( 1<<(s+1) );
        
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
                    w_idx = (n*step/repetitions > (N/2)-1) ? 0 : (n*step/repetitions);  // warning: reinit overhead!

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
    

    if(!mode){
        for(int k=0; k<N; k++) X_mag[k] = abs(x_2[k]);
        for(int i=0; i<N; i++) std::cout << x_2[i] << std::endl; // print output -- DEBUG ONLY
    }
    else {
        for(int k=0; k<N; k++) X_mag[k] = abs(x_1[k]);
        for(int i=0; i<N; i++) std::cout << x_1[i] << std::endl; // print output -- DEBUG ONLY
    }

    std::cout << std::endl;
    for(int i=0; i<N; i++) std::cout << X_mag[i] << std::endl;
    




}

// functions

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


