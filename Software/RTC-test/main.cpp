#include "mbed.h"
#include "stdint.h"
#include <cstdint>
#include <iostream>
#include <string>

// #define DEBUG

////////////////////////// RTC STUFF //////////////////////////

constexpr uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
constexpr uint32_t sd = 86400;
constexpr uint16_t shh = 3600;
constexpr uint8_t smm = 60;


// LCD pin definitions
#define RW 12
#define RS 11
#define EN 13

// push button pin definitions
// buttons A and C on the MSB for now
DigitalIn up_btn(PG_0);
DigitalIn down_btn(PG_2);
DigitalIn confirm_btn(PC_13);    // active high

// timers
Timer btn_tmr;

// time vars
// globals and statics init to 0
uint8_t day = 1; uint8_t month = 1; uint16_t year = 25; uint8_t hour; uint8_t minute;

std::string time_string;    // holds formatted date and time string
char s1[11];                // fit 11 chars on the first row
char s2[14];                // fit 14 chars on the second row

///////////////////////////////////////////////////////////////

// functions
bool isLeap(uint16_t year);
time_t unix_stamp(uint8_t d, uint8_t m, uint16_t y, uint8_t hh, uint8_t mm, uint8_t ss=0);
void lcd_command(unsigned char command);
void lcd_data(char data);
void lcd_write(const char* s);
void lcd_write(uint16_t num);
void lcd_clear();
void lcd_second_line();
void lcd_init();


int main()
{

    // init LCD
    lcd_init();

    #ifndef DEBUG

        // flag to determine if confirm button has been pressed
        bool confirm = 0;

        // setting the day
        while(1) {

            lcd_clear();
            lcd_write("set day: ");
            lcd_write(day);

            //////////////// break logic ////////////////
            if(confirm_btn) {
                btn_tmr.start();
                while(confirm_btn) {

                    if(btn_tmr.elapsed_time() > 200ms) confirm = 1;
                    else {
                        confirm = 0;
                    }
                }
                btn_tmr.stop();
                btn_tmr.reset();
            }
            if(confirm) {
                while(confirm_btn); // wait for release
                confirm = 0;        // reset flag
                break;
            }
            /////////////////////////////////////////////
            
            if(up_btn) {
                day++;
                if(day > 31) day = 1;
            }        
            if(down_btn) {
                day--;
                if(day == 0) day = 31;
            }
            
            wait_us(250000);
        }
        // setting the month
        while(1) {
            
            lcd_clear();
            lcd_write("set month: ");
            lcd_write(month);

            //////////////// break logic ////////////////
            if(confirm_btn) {
                btn_tmr.start();
                while(confirm_btn) {

                    if(btn_tmr.elapsed_time() > 200ms) confirm = 1;
                    else {
                        confirm = 0;
                    }
                }
                btn_tmr.stop();
                btn_tmr.reset();
            }
            if(confirm) {
                while(confirm_btn); // wait for release
                confirm = 0;        // reset flag
                break;
            }
            /////////////////////////////////////////////
            
            if(up_btn) {
                month++;
                if(month > 12) month = 1;
            }           
            if(down_btn) {
                month--;
                if(month == 0) month = 12;
            }
            wait_us(250000);
        }
        // setting the year
        while(1) {
            
            lcd_clear();
            lcd_write("set year: ");
            lcd_write(year);

            //////////////// break logic ////////////////
            if(confirm_btn) {
                btn_tmr.start();
                while(confirm_btn) {

                    if(btn_tmr.elapsed_time() > 200ms) confirm = 1;
                    else {
                        confirm = 0;
                    }
                }
                btn_tmr.stop();
                btn_tmr.reset();
            }
            if(confirm) {
                while(confirm_btn); // wait for release
                confirm = 0;        // reset flag
                break;
            }
            /////////////////////////////////////////////
            
            if(up_btn) year++;            
            if(down_btn) year--;
            
            wait_us(250000);
        }
        // setting the hours
        while(1) {
            
            lcd_clear();
            lcd_write("set hours: ");
            lcd_write(hour);

            //////////////// break logic ////////////////
            if(confirm_btn) {
                btn_tmr.start();
                while(confirm_btn) {

                    if(btn_tmr.elapsed_time() > 200ms) confirm = 1;
                    else {
                        confirm = 0;
                    }
                }
                btn_tmr.stop();
                btn_tmr.reset();
            }
            if(confirm) {
                while(confirm_btn); // wait for release
                confirm = 0;        // reset flag
                break;
            }
            /////////////////////////////////////////////
            
            if(up_btn) {
                hour++;
                if(hour > 23) hour = 0;
            }            
            if(down_btn) {
                hour--;
                if(hour == 255) hour = 23;
            }
            
            wait_us(250000);
        }
        // setting the minutes
        while(1) {
            
            lcd_clear();
            lcd_write("set minutes: ");
            lcd_write(minute);

            //////////////// break logic ////////////////
            if(confirm_btn) {
                btn_tmr.start();
                while(confirm_btn) {

                    if(btn_tmr.elapsed_time() > 200ms) confirm = 1;
                    else {
                        confirm = 0;
                    }
                }
                btn_tmr.stop();
                btn_tmr.reset();
            }
            if(confirm) {
                while(confirm_btn); // wait for release
                confirm = 0;        // reset flag
                break;
            }
            /////////////////////////////////////////////
            
            if(up_btn) {
                minute++;
                if(minute > 59) minute = 0;
            }           
            if(down_btn) {
                minute--;
                if(minute == 255) minute = 59;
            }
            
            wait_us(250000);
        }
    

    
        // date shall be manually set by user
        set_time( unix_stamp(day,month,2000+year,hour,minute) );  // Set RTC time to d/m/y/hh/mm/ss
    #endif

    #ifdef DEBUG
        set_time( unix_stamp(1,2,2025,9,0) );  // Set RTC time to d/m/y/hh/mm/ss
    #endif

    while (true) {
        
        // raw time data stored here
        time_t seconds = time(NULL);

        // obtain formatted date and time string
        time_string = ctime(&seconds);
        
        // splicing to fit on LCD
        for(int i=0; i<11; i++) s1[i] = time_string[i];
        for(int i=0; i<13; i++) s2[i] = time_string[i+11];

        s1[10] = '\0';  // manually place null terminators at the end
        s2[13] = '\0';  //

        // write date and time to LCD
        lcd_clear();
        lcd_write(s1);
        lcd_second_line();
        lcd_write(s2);

        ThisThread::sleep_for(1000ms);
    }
}


bool isLeap(uint16_t year) {

    // A leap year is divisible by 4, but not by 100 unless also divisible by 400.

    // is it div by 4?
    if( !(year%4) ) {
        if( (year%100) || !(year%100) && !(year%400) ) return 1;
        else return 0;
    }
    else return 0;
    

}

// time_t is just an int64_t typdef
time_t unix_stamp(uint8_t d, uint8_t m, uint16_t y, uint8_t hh, uint8_t mm, uint8_t ss) {

    // determine number of "normal" years elapsed since 1970
    uint16_t years_elapsed = y - 1970;
    uint16_t number_of_leap_years = 0;

    // determine number of leap years since 1970
    for(int i=1970; i<y; i++) {
        if(isLeap(i)) number_of_leap_years++;
    }

    // days elapsed thus far
    uint16_t days_elapsed = (365 * years_elapsed) + number_of_leap_years;

    // determine number of days elapsed from Jan1 of the current year
    uint16_t days_elapsed_current = 0;
    for(int i=0; i<(m-1); i++) days_elapsed_current += days[i];

    // determine number of days elapsed in current month
    uint16_t days_elapsed_month = d-1;

    // determine total days elapsed
    uint16_t days_elapsed_total = days_elapsed + days_elapsed_current + days_elapsed_month;

    time_t seconds_elapsed = (sd * days_elapsed_total) + (shh * hh) + (smm * mm) + (ss);


    return seconds_elapsed;


}

void lcd_command(unsigned char command) {
	
	//this function sends command to LCD (4 bit mode)
	
	//RW = 0;
	GPIOD->ODR &= ~(1u<<RW); //PD12 is RW
	//RS = 0;
	GPIOD->ODR &= ~(1u<<RS); //PD11 is RS
	
	//writing first 4 bits - 4 bit mode!
	GPIOD->ODR |= (command & 0xF0);
	
	//EN pulse
	GPIOD->ODR |= 1<<EN; //PD13 is EN - EN on
	wait_us(2); //2us melay
	GPIOD->ODR &= ~(1u<<EN); //EN off
	GPIOD->ODR &= ~(255u<<0); // clear D0-D7
	
	//writing second 4 bits - 4 bit mode!
	GPIOD->ODR |= ((command<<4) & 0xF0);
	
	//EN pulse
	GPIOD->ODR |= 1<<EN; //PD13 is EN - EN on
	wait_us(2); //2us melay
	GPIOD->ODR &= ~(1u<<EN); //EN off
	GPIOD->ODR &= ~(255u<<0); // clear D0-D7
	
	if (command < 4) {
		
		wait_us(2000); // commands 1 and 2 needs up to 1.64ms ~2ms
	}
	
	else {
		
		wait_us(40); // all other commands need up tp 40us
		
	}
	
	GPIOD->ODR &= ~(255u<<0); // clear D0-D7
	
}

void lcd_data(char data) {
	
	//this function sends data to LCD (4 bit mode)
	
	//RS = 1;
	GPIOD->ODR |= 1<<RS; //PD11 is RS
	
	//RW = 0;
	GPIOD->ODR &= ~(1u<<RW); //PD12 is RW
	
	
	//writing first 4 bits - 4 bit mode!
	GPIOD->ODR |= (data & 0xF0);
	
	//EN pulse
	GPIOD->ODR |= 1<<EN; //PD13 is EN - EN on
	wait_us(2); //2us melay
	GPIOD->ODR &= ~(1u<<EN); //EN off
	GPIOD->ODR &= ~(255u<<0); // clear D0-D7

	
	//writing second 4 bits - 4 bit mode!
	GPIOD->ODR |= ((data<<4) & 0xF0);
	
	//EN pulse
	GPIOD->ODR |= 1<<EN; //PD13 is EN - EN on
	wait_us(2); //2us melay
	GPIOD->ODR &= ~(1u<<EN); //EN off
	GPIOD->ODR &= ~(255u<<0); // clear D0-D7

	
	
	wait_us(500);
	
}

void lcd_write(const char* s) {

    uint8_t sz = 0;

    // determine the size of the string literal passed
    for(int i=0; i < 255; i++) {

        if(s[i] == '\0') break;
        else sz++;
    }

    // write to LCD
    for(int i=0; i<sz; i++) lcd_data(s[i]);

}

// overrided version to take numbers
void lcd_write(uint16_t num) {

    std::string s = std::to_string(num);
    lcd_write(s.c_str());   // passes it as const char*
}

void lcd_init() {

    // GPIOD clock enable
    RCC->AHB1ENR |= 1<<3; 

    // setting GPIOD pins (LCD) to output mode //
	
    GPIOD->MODER |= 1<<8;   // lcd_d0_Pin (PD4)
	GPIOD->MODER |= 1<<10;  // lcd_d1_Pin (PD5)
	GPIOD->MODER |= 1<<12;  // lcd_d2_Pin (PD6)
	GPIOD->MODER |= 1<<14;  // lcd_d3_Pin (PD7)
	GPIOD->MODER |= 1<<22;  // RS_Pin (PD11)
	GPIOD->MODER |= 1<<24;  // RW_Pin (PD12)
	GPIOD->MODER |= 1<<26;  // E_Pin (PD13)
	GPIOD->MODER |= 1<<28;  // BKL_Pin (PD14)

    // for data pins only: PD0-PD7
	GPIOD->OTYPER |= 1<<0 | 1<<1 | 1<<2 | 1<<3 | 1<<4 | 1<<5 | 1<<6 | 1<<7;     // open drain
	GPIOD->PUPDR |= 1<<0 | 1<<2 | 1<<4 | 1<<6 | 1<<8 | 1<<10 | 1<<12 | 1<<14;   // pull up resistors

    GPIOD->ODR |= 1<<14;    // lcd backlight
	lcd_command(0x2C);      // 4 bit, 2 line, 5x11 font
	lcd_command(0x06);      // move cursor to right after each character
	lcd_command(0x01);      // clear screen, move cursor to home
	lcd_command(0x0F);      // turn on display, cursor blinking

}

void lcd_clear() {
    lcd_command(0x01);
}

void lcd_second_line() {
    lcd_command(0xC0);
}