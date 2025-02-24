#include "mbed.h"
#include "stdint.h"
#include <iostream>



// main() runs in its own thread in the OS
int main()
{

    // this UNIX timestamp shall be determined via an algorithm
    // date shall be manually set by user
    set_time(1256729737);  // Set RTC time to Wed, 28 Oct 2009 11:35:37

    while (true) {
        
        time_t seconds = time(NULL);

        printf("Time as seconds since January 1, 1970 = %u\n", (unsigned int)seconds);

        printf("Time as a basic string = %s", ctime(&seconds));

        char buffer[32];
        strftime(buffer, 32, "%I:%M %p\n", localtime(&seconds));
        printf("Time as a custom formatted string = %s", buffer);

        ThisThread::sleep_for(1000);
    }
}

