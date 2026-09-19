#include "eeprom.h"
#include <Wire.h>

#ifndef EEPROM_ADDR
#define EEPROM_ADDR 0x50
#endif


void writeEEPROM(unsigned int eeaddress, byte *data, int length) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((int)(eeaddress >> 8));
    Wire.write((int)(eeaddress & 0xFF));
    for (int i = 0; i < length; i++) {
        Wire.write(data[i]);
    }
    Wire.endTransmission();
    delay(5);
}

void readEEPROM(unsigned int eeaddress, byte *buffer, int length) {
    int done = 0;
    while (done < length) {
        int n = length - done;
        if (n > 64) n = 64;
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((int)((eeaddress + done) >> 8));
        Wire.write((int)((eeaddress + done) & 0xFF));
        Wire.endTransmission();
        Wire.requestFrom((uint8_t)EEPROM_ADDR, (uint8_t)n);
        for (int i = 0; i < n; i++) {
            if (Wire.available()) buffer[done + i] = Wire.read();
        }
        done += n;
        delay(2);
    }
}
