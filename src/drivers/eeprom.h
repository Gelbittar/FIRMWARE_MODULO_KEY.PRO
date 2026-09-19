#ifndef GEYLCA_DRIVERS_EEPROM_H
#define GEYLCA_DRIVERS_EEPROM_H

#include <Arduino.h>

void writeEEPROM(unsigned int eeaddress, byte *data, int length);
void readEEPROM(unsigned int eeaddress, byte *buffer, int length);

#endif
