#include <iostream>
#include <complex>
#include <cmath>
#include <vector>

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

int main() {
    // Example input signal (can be modified)
    vector<double> input_samples = {1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0};
    for (size_t i = 0; i < input_samples.size(); i++) {
        x[i] = input_samples[i];
    }
    
    compute_twiddle_factors();
    fft(x);
    
    // Print FFT magnitude results
    for (const auto& val : x) {
        cout << abs(val) << endl;
    }
}
