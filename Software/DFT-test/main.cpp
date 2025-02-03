#include "mbed.h"
#include <iostream>
#include <complex>
#include <cmath>

using std::complex;
using std::pow;

constexpr float pi = 3.14159;
constexpr uint8_t N = 1<<2; // ensures it is a power of 2


complex<double> z3(0,-2*pi/N); // -2*pi*j/N
complex<double> w = exp(z3);    // twiddle factor, w = e^(-2*pi*j/N)


constexpr double x[N] = {1,5,4,2}; // input time domain
complex<double> X[N]; // output frequency domain; uninit globals and statics are zero
double X_mag[N]; // used for spectrum estimation


int main() {


    for (int k=0; k<N; k++) {
        for(int n=0; n<N; n++) {

           X[k] += x[n] * pow(w,n*k);

        }
    }

    for(int k=0; k<N; k++) X_mag[k] = abs(X[k]);

    for(int i=0; i<N; i++) std::cout << X[i] << std::endl;
    for(int i=0; i<N; i++) std::cout << X_mag[i] << std::endl;
    




}
