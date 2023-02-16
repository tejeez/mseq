#!/bin/sh
# Check if a pin is already used for something.
grep -n "$1" "src/main.cpp" "${HOME}/.platformio/packages/framework-mbed/targets/TARGET_STM/TARGET_STM32H7/TARGET_STM32H743xI/TARGET_NUCLEO_H743ZI/PinNames.h"
