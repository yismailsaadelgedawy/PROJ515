#ifndef SDCARD_HPP
#define SDCARD_HPP

#include <string>
using namespace std;

// Group Y
// Authors: Youssef Elgedawy, Sayeed Rahman and Rhys Jones

int mywrite_sdcard(int value, string time);   //SD Card write function
int mywrite_sdtime(string time);                                                    //SD Card write time function (experimental)            
int myread_sdcard();                                                                //Read SD Card, not used, not required (all reading done during coding was with SD Card Reader)
int write_error(int time, int errortype);                                           //Write Error type and time of occurrance to file function
int SDCardStatus(int temp, int pressure, int humidity, int light, bool stage);      //Status of SD card (experimental, initial idea, since removed)
void sd_print_begin();                                                              //Reduce excessive write initalise printing
void sd_print_end();                                                                //Reduce excessive write complete printing


#endif
