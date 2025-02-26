#include "mbed.h"
#include <cstdio>
#include <iostream>
#include <string>
using namespace std;

// Group Y
// Authors: Youssef Elgedawy, Sayeed Rahman and Rhys Jones

#define USE_SD_CARD

// SD Card (RTOS ONLY)
#ifdef USE_SD_CARD
#include "SDBlockDevice.h"
#include "FATFileSystem.h"
// Instantiate the SDBlockDevice by specifying the SPI pins connected to the SDCard
// socket. The PINS are: (This can also be done in the JSON file see mbed.org Docs API Storage SDBlockDevice)
// PB_5    MOSI (Master Out Slave In)
// PB_4    MISO (Master In Slave Out)
// PB_3    SCLK (Serial Clock)
// PF_3    CS (Chip Select)
//
// and there is a Card Detect CD on PF_4 ! (NB this is an Interrupt capable pin..)
SDBlockDevice sd1(PB_5, PB_4, PB_3, PF_3);



void sd_print_begin() {                         //Prevent printing to the terminal excessively when writing many cycles in a row

    printf("Initialise and write to a file\n");


}

void sd_print_end() {                           //Prevent printing to the terminal excessively when writing many cycles in a row

    printf("SD Write done...\n");


}

int mywrite_sdcard(int value, string time)    //Write function for SD Card
{
    
    int err;
    // call the SDBlockDevice instance initialisation method.

    err=sd1.init();                                                                 //Detect init Error
    if ( 0 != err) {
        printf("Init failed %d\n",err);                                             //Print Error to terminal
        return -1;
    }
    
    FATFileSystem fs("sd", &sd1);                                                   
    FILE *fp = fopen("/sd/HiveFrequencyData.txt","a");                                     //Open File for sensor data
    if(fp == NULL) {                                                                //If FP Null
        error("Could not open file for write\n");                                   //File opening error
        sd1.deinit();                                                               //De init
        return -1;
    } else {
       char stringbuffer[50];                                                       //To write string to terminal, create array of characters (%s with raw string doesn't work)
        for(int i=0; i<50; i++){                                                    //Fill Array with each character from the string
            stringbuffer[i] = time[i];
        }
        //Put some text in the file...
        fprintf(fp, "%s   %d\n", stringbuffer, value);
        //Tidy up here
        fclose(fp);                                                                 //Close File
       
        
        sd1.deinit();                                                               //De init
       
        return 0;
    }
    
}

int mywrite_sdtime(string time)                                                     //Experimental function for only printing string time
{                                                                                   //Same functionality and comments as prev
    
    
    
    int err;
    // call the SDBlockDevice instance initialisation method.

    err=sd1.init();                                                                 //Detect Init error
    if ( 0 != err) {
        printf("Init failed %d\n",err);                                             //Print error
        return -1;
    }
    
    FATFileSystem fs("sd", &sd1);
    FILE *fp = fopen("/sd/SensorData.txt","a");                                     //Open Sensor data file
    if(fp == NULL) {                                                                //If null, file cannot be opened
        error("Could not open file for write\n");
        sd1.deinit();
        return -1;
    } else {
       
        //Put some text in the file...
        char stringbuffer[50];                                                      //String an array of characters to print
        for(int i=0; i<50; i++){
            stringbuffer[i] = time[i];
            fprintf(fp, "Date is : %s", stringbuffer[i]);                           //Print time
        }
        //Tidy up here
        fclose(fp);                                                                 //Close file
       
        
        sd1.deinit();                                                               //De init
       
        return 0;
    }
    
}


// Used for Error Documentation
int write_error(int time, int errortype)                                            //Write error function to log errors at a certain time when occurring
{
    
    printf("Critical Error\n");                                                     //Print critical error when called
    int err;
    // call the SDBlockDevice instance initialisation method.

    err=sd1.init();                                                                 //If cannot initialise print failed init
    if ( 0 != err) {
        printf("Init failed %d\n",err);
        return -1;
    }
    
    FATFileSystem fs("sd", &sd1);                                                   //Open file error.txt
    FILE *fp = fopen("/sd/error.txt","a");
    if(fp == NULL) {                                                                //If null, file cannot open for error logging
        error("Could not open file for Error Logging\n");
        sd1.deinit();                                                               //De init
        return -1;
    } else {
        
        //Put some text in the file...                                              //Error type if cascade to choose error type based on parsed integer
        if (errortype == 1){
            fprintf(fp, "Critical Error: Buffer is FUll  \n");
        }else if (errortype == 2){
            fprintf(fp, "Critical Error: Deadlock  \n");    
        }else if (errortype == 3){
            fprintf(fp, "Critical Error: Race Condition \n");
        }else if (errortype == 4){
            fprintf(fp, "Critical Error: Network Failed to Connect \n");
        }else{
            fprintf(fp, "Critical Error: Unknown  \n");
        }
        // char stringbuffer[50];                                                   //String an array of characters to print
        // for(int i=0; i<50; i++){
        //     stringbuffer[i] = time[i];
        fprintf(fp, "Date is\n", time);                                             //Add date as well
        //Tidy up here
        fclose(fp);                                                                 //Close file
        printf("Error Write done...\n");                                            //Error write done
        sd1.deinit();                                                               //De init
        
        return 0;
    }
    
}


#endif