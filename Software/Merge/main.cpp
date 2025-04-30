#include "mbed.h"
#include <cstdint>
#include <iostream>
#include <complex>
#include <cmath>
#include <chrono>

// FFT parameters
#define test_frequency                      400                     // a test frequency used to ensure FFT works as intended
#define piping_frequency                    400                     // the frequency of queen piping
#define piping_detection_threshold          80                      // the magnitude required to be classified as "ON" (100% volume monitor speakers)
#define piping_off_threshold                50                      // the magnitude required to be classified as "OFF" (100% volume monitor speakers)
#define predator_frequency                  240                     // the frequency of predator
#define predator_detection_threshold        800                    // the magnitude required to be classified as "ON" (100% volume monitor speakers)
#define predator_off_threshold              600                     // the magnitude required to be classified as "OFF" (100% volume monitor speakers)

// Temperature parameters
#define samples                             4                       // number of samples for moving average
#define lower_temp_th                       30.0f                   // lower threshold for brood temp range (easier testing; 34.5f actual)
#define upper_temp_th                       35.5f                   // upper threshold for brood temp range
#define total_number_of_sensors             16                      // total number of sensors used in array
#define sense_interval                      1s                      // how often temperature is measured
#define addr0                               PG_2                    // MUX addresses addr[3:0]
#define addr1                               PG_3                    //
#define addr2                               PD_0                    //
#define addr3                               PD_1                    //

// IO
AnalogIn acc(PA_3);                                         // acc input
AnalogIn mic(PC_3);                                         // micR input
// DigitalOut samp_pin(NC);                                    // test output pin
DigitalOut red(PB_14);                                      // red LED
DigitalOut green(PB_0);                                     // green LED
DigitalOut blue(PB_7);                                      // blue LED
BusOut addr(addr0, addr1, addr2, addr3);                    // LSB -> MSB
AnalogIn temp_sense(PF_5);
// DigitalOut maint_LED(PE_3);

// Setup SPI Slave: mosi, miso, sclk, ssel
// PI SPI MUST be a master
// so PI must send dummy data to generate the clock
// so that the MCU can send its data
SPISlave spi4_slave(PE_14, PE_13, PE_12, PE_11); // MOSI, MISO, SCLK, CS

// SPI handler
SPI_HandleTypeDef hspi4;

// Int pins
InterruptIn piping_pin(PA_5);
InterruptIn hornet_pin(PA_6);
InterruptIn maint_pin(PF_13);

// Timers
Timer tmr;              // general timer
Ticker t;               // sampling timer
Timer tmr_p;            // timer used for piping
Timer tmr_pred;         // timer used for predator detection
Timer tmr_fft;          // tuner timer used to run fft for a while after trigger
Timer tmr_fft2;         // tuner timer used to run fft for a while after trigger

// timing
constexpr uint16_t fs = 8192;                               // sampling frequency
constexpr std::chrono::microseconds Ts(1000000/fs);         // sampling time period (us)
uint8_t loop_time_state_p;                                  // used to control loop time calculation
uint8_t loop_time_state_pred;                               // 



//////////////////////////////////// FFT STUFF ////////////////////////////////////

using std::complex;
using std::pow;


constexpr float pi = 3.14159;   // pi :)
constexpr uint16_t N = 1<<9;    // ensures it is a power of 2

constexpr double f_res = (1.0f)/(N * 1.0f/fs);                          // frequency resolution (currently it is 8Hz - good enough; saves memory)
constexpr uint16_t k_test = test_frequency/(uint16_t)f_res;             // obtains the frequency bin, k, corresponding to test_frequency
constexpr uint16_t k_p = piping_frequency/(uint16_t)f_res;              // obtains the frequency bin, k, corresponding to piping_frequency
constexpr uint16_t k_pred = predator_frequency/(uint16_t)f_res;         // obtains the frequency bin, k, corresponding to predator_frequency


constexpr complex<double> z3(0,-2*pi/N);    // -2*pi*j/N
const complex<double> w = exp(z3);          // twiddle factor, w = e^(-2*pi*j/N) - (exp isn't a constexpr)

volatile double x[N];                       // input time domain
complex<float> x_1[N];                      // array 1 - time domain (scrambled)
complex<float> x_2[N];                      // array 2 - time domain; two arrays due to constant geometry
complex<float> W_array[N/2];                // twiddle factors (precomputed)
complex<float> mul;                         // holds the result of the W*B product; optimises 2 multiplications down to 1 
volatile uint16_t mag1,mag2,mag_avg;        // 2 magnitude samples (n and n-1), and an average of them
volatile bool avg = 1;                      // controls whether moving average is enabled or not; enabled by default

constexpr uint32_t fft_time_us = 5000000;   // sets how long the FFT to run for after the trigger (5 seconds by default)

///////////////////////////////////////////////////////////////////////////////////

/////////////////// PIPING STUFF ////////////////////

// piping counters
uint8_t cnt_long_pulse_p;           // determines how long the "long piping pulse" is
uint8_t cnt_short_pulse_p;          // counts number of shorter piping pulses
// piping flags
bool long_pulse_p;                  // was a long pulse detected?
bool piping_detected;               // was piping detected?
// variables
uint8_t long_samples_expected_p;
constexpr uint8_t short_samples_expected_p = 6;
// parameters
constexpr uint16_t long_pulse_duration_ms_p = 800;
constexpr uint16_t short_pulses_duration_ms_p = 5000;

//////////////////////////////////////////////////////

//////////////////// PREDATOR STUFF ////////////////////

// predator counters
uint8_t cnt_long_pulse_pred;            // determines how long the "long predator pulse" is
// predator flags
bool pred_detected;                     // was a predator detected?
// variables
uint8_t long_samples_expected_pred;
// parameters
constexpr uint16_t long_pulse_duration_ms_pred = 800;

//////////////////////////////////////////////////////

//////////////////// TEMPERATURE STUFF ////////////////////

float temp[samples];
uint8_t temp_idx;
float temp_avg;

bool sensors_in_brood_area[total_number_of_sensors];
bool inBroodArea;
uint8_t sum;

///////////////////////////////////////////////////////////

////////// DEBUG STUFF //////////

// #define DEBUG_P            // debug prints on/off
// #define TUNING_P           // tuning prints on/off
#define PIPING           // piping algorithm on/off
#define PIPING_DEBUG     // piping debug prints on/off
#define FFT_TIMING_P           // timed FFT on/off

// #define DEBUG_PRED                // debug prints on/off
// #define TUNING_PRED               // tuning prints on/off
#define PREDATOR             // predator algorithm on/off
#define PREDATOR_DEBUG       // predator debug prints on/off
#define FFT_TIMING_PRED           // timed FFT on/off

#define DEBUG_TEMP

/////////////////////////////////

// Thread parameters
#define piping_trigger 1
#define predator_trigger 2
#define samp 3

// Threads
Thread t1;              // accelerometer thread
Thread t2;              // microphone thread
Thread t3;              // temperature thread
Thread t4;              // SPI thread

// Mutexes
Mutex fft_mtx;          // used for FFTs

// Thread functions
void acc_thread();
void mic_thread();
void temp_thread();
void SPI_thread();


// functions
uint32_t bit_reverse(uint32_t num, int numBits);            // bit scrambling algorithm
bool sense(int sensor_number);                              // temperature sensing
void spi4_init();
void spi4_write(uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4);

// ISRs
void sampling_ISR();                                        // sampling interrupt service routine
void trigger_filter_piping_ISR();                           // trigger filter interrupt service routine from PCB
void trigger_filter_hornet_ISR();                           // trigger filter interrupt service routine from PCB
// void maint_on_ISR();
// void maint_off_ISR();

// main() runs in its own thread in the OS
int main()
{

    // setup maintenance isrs
    // maint_pin.rise(maint_on_ISR);
    // maint_pin.fall(maint_off_ISR);

    // main thread runs first

    // start threads
	t1.start(acc_thread);  
	t2.start(mic_thread);  
    t3.start(temp_thread);
    t4.start(SPI_thread);

    // waits for threads to finish
	// which they won't...
	t1.join();

    std::cout << "---- FATAL ERROR: MAIN EXITED ----\n" << std::endl;

    return 0;
}

/// threads ///

// t1
void acc_thread() {

    // precompute twiddle factors - ONCE
    // optimises for speed
    for (int i=0; i<(N/2); i++) {
        
        W_array[i] = pow(w,i);          // W^0, W^1, ... W^(N/2 - 1)
    }

    #ifdef FFT_TIMING_P
        // setup the trigger ISR
        piping_pin.rise(trigger_filter_piping_ISR);
        ThisThread::flags_wait_all(piping_trigger); // waits for flag from the piping ISR
        ThisThread::flags_clear(piping_trigger);    // clear flag

        fft_mtx.lock(); // enter critical section; THREAD START 1
    #endif


    while(1) {

        #ifdef FFT_TIMING_P

            // after triggering, run FFT for a bit
            tmr_fft2.start();
            
            if(tmr_fft2.elapsed_time().count() > fft_time_us) {

                tmr_fft2.stop();                                    // stop timer
                tmr_fft2.reset();                                   // reset timer
                t.detach();                                         // disable fft
                piping_pin.rise(trigger_filter_piping_ISR);         // re-enable the trigger interrupt
                fft_mtx.unlock();                                   // exit critical section; THREAD END
                ThisThread::flags_clear(piping_trigger);          // clear flag
                ThisThread::flags_wait_all(piping_trigger);         // waits for flag from the piping ISR
                ThisThread::flags_clear(piping_trigger);            // clear flag
                fft_mtx.lock();                                     // enter critical section; THREAD START 2
                tmr_fft2.start();                                   // start timer again
                
            }
        #endif
        
        
        t.attach(sampling_ISR, Ts);         // setup sampling ISR
        ThisThread::flags_wait_all(samp);   // waits for flag from the piping ISR
        ThisThread::flags_clear(samp);      // clear flag

        // start reading values...
        for(int n=0; n<N; n++) {
            x[n] = (acc.read() - 0.5) * 100.0f;     // 0.5 to remove DC offset; scaled to make numbers nicer to work with
            ThisThread::flags_wait_all(samp);       // waits for flag from the piping ISR
            ThisThread::flags_clear(samp);          // clear flag
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
        #ifdef DEBUG_P
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
        if(loop_time_state_p == 1) {
            tmr.stop();
            uint16_t loop_time_ms = tmr.elapsed_time().count()/1000;
            std::cout << "Loop time (ms): " << loop_time_ms << std::endl;
            tmr.reset();
            loop_time_state_p = 2;

            // calculate the number of expected long piping samples
            // within a 1sec time window; function of loop time!
            long_samples_expected_p = long_pulse_duration_ms_p/loop_time_ms;

            #ifdef PIPING_DEBUG
                std::cout << "long pulses expected: " << (int)long_samples_expected_p << std::endl;
                std::cout << "short pulses expected: " << (int)short_samples_expected_p << std::endl;
            #endif
        }

        // --- DO NOT PUT CODE HERE --- //

        // may enable/disable moving average (2 samples) at will
        if(avg) mag_avg = (mag1+mag2)/2;
        else mag_avg = mag1;

        // --- DO NOT PUT CODE HERE --- //

        // calculation of loop time (start)
        // how long it takes to obtain a magnitude in the frequency domain
        if(loop_time_state_p == 0) {
            tmr.start();
            loop_time_state_p = 1;
        }
        
        
        // simple test for tuning
        // to determine piping_detection_threshold
        #ifdef TUNING
            if(mag_avg > piping_detection_threshold) {
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
                
                if(!long_pulse_p) {

                    // first, detect the f
                    if(mag_avg > piping_detection_threshold) {
                        tmr_p.start();      // start 1 second window
                        cnt_long_pulse_p++;   // increment when f is detected
                    }
                    // when window elapses, determine whether it was the long piping pulse or not
                    if(tmr_p.elapsed_time().count()/1000 > long_pulse_duration_ms_p) {
                        tmr_p.stop();
                        tmr_p.reset();

                        #ifdef PIPING_DEBUG
                            std::cout << "long: " << (int)cnt_long_pulse_p << std::endl;
                        #endif

                        if(cnt_long_pulse_p >= long_samples_expected_p) {
                            long_pulse_p = 1;     // long pulse has been detected
                            cnt_long_pulse_p = 0; // reset counter
                            red = 1;
                        }
                        cnt_long_pulse_p = 0;     // reset counter
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
                    if(mag1 > piping_detection_threshold && mag2 < piping_off_threshold) cnt_short_pulse_p++;
                    
                    // when window elapses, determine whether it was the long piping pulse or not
                    if(tmr_p.elapsed_time().count()/1000 > short_pulses_duration_ms_p) {
                        tmr_p.stop();
                        tmr_p.reset();

                        avg = 1;    // re-enable averaging

                        #ifdef PIPING_DEBUG
                            std::cout << "short: " << (int)cnt_short_pulse_p << std::endl;
                        #endif

                        if(cnt_short_pulse_p >= short_samples_expected_p) {
                            piping_detected = 1;    // piping is now detected
                            cnt_short_pulse_p = 0;    // reset counter
                        }
                        else {
                            long_pulse_p = 0;         // reset flag; restart the process; it wasn't piping
                            red = 0;                // reset indicator LED
                        }   
                        cnt_short_pulse_p = 0;        // reset counter
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

// t2
void mic_thread() {

    // precompute twiddle factors - ONCE
    // optimises for speed
    for (int i=0; i<(N/2); i++) {
        
        W_array[i] = pow(w,i);          // W^0, W^1, ... W^(N/2 - 1)
    }

    #ifdef FFT_TIMING_PRED
        // setup the trigger ISR
        hornet_pin.rise(trigger_filter_hornet_ISR);

        ThisThread::flags_wait_all(predator_trigger);   // waits for flag from the predator ISR
        ThisThread::flags_clear(predator_trigger);      // clear flag

        fft_mtx.lock(); // enter critical section; THREAD START 1
    #endif
    

    while(1) {

        #ifdef FFT_TIMING_PRED
            // after triggering, run FFT for a bit
            tmr_fft.start();

            if(tmr_fft.elapsed_time().count() > fft_time_us) {

                tmr_fft.stop();                                     // stop timer
                tmr_fft.reset();                                    // reset timer
                t.detach();                                         // disable fft
                hornet_pin.rise(trigger_filter_hornet_ISR);         // re-enable the trigger interrupt
                fft_mtx.unlock();                                   // exit critical section; THREAD END
                ThisThread::flags_clear(predator_trigger);          // clear flag
                ThisThread::flags_wait_all(predator_trigger);       // waits for flag from the predator ISR
                ThisThread::flags_clear(predator_trigger);          // clear flag
                fft_mtx.lock();                                     // enter critical section; THREAD START 2
                tmr_fft.start();                                    // start timer again
                
            }
        #endif

        // std::cout << "running" << std::endl;
        
        t.attach(sampling_ISR, Ts);                         // setup sampling ISR
        ThisThread::flags_wait_all(samp);       // waits for flag from sampling ISR
        ThisThread::flags_clear(samp);          // clear flag

        // start reading values...
        for(int n=0; n<N; n++) {
            x[n] = (mic.read() - 0.5) * 100.0f;     // 0.5 to remove DC offset; scaled to make numbers nicer to work with
            ThisThread::flags_wait_all(samp);       // waits for flag from sampling ISR
            ThisThread::flags_clear(samp);          // clear flag
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
        #ifdef DEBUG_PRED
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
        if(loop_time_state_pred == 1) {
            tmr.stop();
            uint16_t loop_time_ms = tmr.elapsed_time().count()/1000;
            std::cout << "Loop time (ms): " << loop_time_ms << std::endl;
            tmr.reset();
            loop_time_state_pred = 2;

            // calculate the number of expected long predator samples
            // within a 1sec time window; function of loop time!
            long_samples_expected_pred = long_pulse_duration_ms_pred/loop_time_ms;
        }

        // --- DO NOT PUT CODE HERE --- //

        // may enable/disable moving average (2 samples) at will
        if(avg) mag_avg = (mag1+mag2)/2;
        else mag_avg = mag1;

        // --- DO NOT PUT CODE HERE --- //

        // calculation of loop time (start)
        // how long it takes to obtain a magnitude in the frequency domain
        if(loop_time_state_pred == 0) {
            tmr.start();
            loop_time_state_pred = 1;
        }
        
        
        // simple test for tuning
        // to determine predator_detection_threshold
        #ifdef TUNING_PRED
            // if(mag_avg > predator_detection_threshold) {
            //     green = 1;
            //     std::cout << mag_avg << std::endl;
            // }
            // else green = 0;

            std::cout << mag_avg << std::endl;
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
                if(mag_avg > predator_detection_threshold) {
                    tmr_pred.start();      // start 1 second window
                    cnt_long_pulse_pred++;   // increment when f is detected
                }
                // when window elapses, determine whether it was the long predator pulse or not
                if(tmr_pred.elapsed_time().count()/1000 > long_pulse_duration_ms_pred) {
                    tmr_pred.stop();
                    tmr_pred.reset();

                    #ifdef PREDATOR_DEBUG
                        std::cout << "long: " << (int)cnt_long_pulse_pred << std::endl;
                    #endif

                    if(cnt_long_pulse_pred >= long_samples_expected_pred) {
                        cnt_long_pulse_pred = 0; // reset counter
                        pred_detected = 1;
                    }
                    cnt_long_pulse_pred = 0;     // reset counter
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

// t3
void temp_thread() {

    // address = 0 initially -> first sensor selected
    addr = 0;

    while (true) {

        // determine the sensors within the brood area
        if( sense(addr) ) sensors_in_brood_area[addr] = 1;
        else sensors_in_brood_area[addr] = 0;

        // print out the number of sensors within the brood area
        for(int i=0; i<total_number_of_sensors; i++) sum += sensors_in_brood_area[i];

        #ifndef DEBUG_TEMP
            std::cout << "Number of sensors in brood area: " << (int)sum << std::endl;
        #endif

        sum = 0;            // reset sum
        addr = addr + 1;    // select next sensor

        // do something if number is small
        

        ThisThread::sleep_for(sense_interval);    // wait a bit

    }

    
}

// t4
void SPI_thread() {

    spi4_init();

    while (true) {

        // continously calling SPI so mcu is always ready when pi talks
        spi4_write(24, 100, 0x03, 0);
       
            
    }

}


/// ISRs ///

// sampling ISR
void sampling_ISR() {

    // samp_pin = !samp_pin;   // toggle test pin (for probing)
    t1.flags_set(samp); // sends flag to start fft to threads, t1 and t2
    t2.flags_set(samp); //

}

void trigger_filter_piping_ISR() {

    piping_pin.rise(NULL);  // detach to avoid queueing
    t1.flags_set(piping_trigger); // sends flag to acc thread, t1

}

void trigger_filter_hornet_ISR() {

    hornet_pin.rise(NULL);  // detach to avoid queueing
    t2.flags_set(predator_trigger); // sends flag to mic thread, t2

}

// void maint_on_ISR() {
//     maint_LED = 1;
// }

// void maint_off_ISR() {
//     maint_LED = 0;
// }

/// FUNCTIONS ///

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

// temperature measurement function
bool sense(int sensor_number) {

    while(1) {

        // take raw reading
        temp[temp_idx] = (temp_sense.read() * 3.3f) / 0.0825f;  // 82.5mV/deg C
        temp_idx++;

        // once buffer is filled
        if(temp_idx == samples) {

            // reset index
            temp_idx = 0;
            // sum var
            float sum = 0;

            // calculate moving average
            for(int i=0; i<samples; i++) sum += temp[i];
            temp_avg = sum/samples;

            break;
            
        }

    }

    #ifdef DEBUG_TEMP

        std::cout << "Sensor " << sensor_number + 1 << ": " << temp_avg << std::endl;

    #endif

    
    // brood temp range check
    if(temp_avg > lower_temp_th && temp_avg < upper_temp_th) inBroodArea = 1;
    else inBroodArea = 0;
    
    

    return inBroodArea;
}

void spi4_init(void) {

    hspi4.Instance = SPI4;
    hspi4.Init.Mode = SPI_MODE_SLAVE;
    hspi4.Init.Direction = SPI_DIRECTION_2LINES;
    hspi4.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi4.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi4.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi4.Init.NSS = SPI_NSS_HARD_INPUT;
    hspi4.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi4.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi4.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi4.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&hspi4) != HAL_OK) printf("ERROR\n");
}

void spi4_write(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {

    uint8_t txBuf[5] = {a, b, c, d};
    uint8_t rxBuf[5] = {0, 0, 0, 0};

    if (HAL_SPI_TransmitReceive(&hspi4, txBuf, rxBuf, 4, HAL_MAX_DELAY) != HAL_OK) printf("SPI TRANSFER ERROR\n");

}
