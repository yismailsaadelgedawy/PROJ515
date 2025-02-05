#include <iostream>
// #include "header.hpp"
// #include "header2.hpp"

/* This is equivalent to the following:
static int var = 2; // from header1.hpp
static int var = 3; // from header2.hpp

ODR violation!!! */

// NOT globally accessible, but limited to this file/translation unit
static int num = 100;

extern int num2 = 2;


void print_num() {
    std::cout << num << std::endl;
    // print_header();
}