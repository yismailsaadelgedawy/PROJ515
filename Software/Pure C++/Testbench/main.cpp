#include <iostream>
#include "stdint.h"
#include "string.h"
#include <functional>
#include "test.hpp"
#include "header.hpp"
#include <vector>
#include <chrono>
#include <cmath>

uint32_t bit_reverse(uint32_t num, int numBits);


constexpr uint8_t N = 8;

int main() {


    // for (int s=0; s<log2(N); s++) {

    //     for (int n=0; n<(N/2); n++) {

    //         std::cout << ( (N)/(1<<(s+1)) ) * (n%(1<<s)) << ",";
    //     }

    //     std::cout << std::endl << std::endl;
         

    // }

    // alt

    for (int s=1; s<=log2(N); s++) {

        for (int n=0; n<(N/2); n++) {

            std::cout << ((n%(1<<s))/(1<<(s-1))) * (N/(1<<s)) << ",";
        }

        std::cout << std::endl << std::endl;
         

    }

    





    // Testing bit-reversal for 3-bit numbers (for an 8-point FFT)

    // int numBits = 4;  // log2(8) = 3 bits
    // for (uint32_t i = 0; i < (1 << numBits); i++) {
    //     printf("Original: %u (%03b) -> Reversed: %u (%03b)\n",
    //             i, bit_reverse(i, numBits));
    // }


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


