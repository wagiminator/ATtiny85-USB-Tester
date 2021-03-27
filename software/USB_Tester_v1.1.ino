// USB Power Tester - Basic
//
// This code implements the basic functionality for the USB Power Tester.
// It reads voltage, current and power from the INA219, calculates capacity
// and shows the values on the OLED. It uses a reduced character set in order
// to make it fit on an ATtiny45. The SET button is used to switch between
// the "recent value screen" and the "min/max value screen".
//
//                         +-\/-+
// RESET --- A0 (D5) PB5  1|    |8  Vcc
// SET ----- A3 (D3) PB3  2|    |7  PB2 (D2) A1 ---- OLED/INA (SCK)
//           A2 (D4) PB4  3|    |6  PB1 (D1) 
//                   GND  4|    |5  PB0 (D0) ------- OLED/INA (SDA)
//                         +----+
//
// Controller:  ATtiny45/85
// Core:        ATTinyCore (https://github.com/SpenceKonde/ATTinyCore)
// Clockspeed:  8 MHz internal
// Millis:      enabled
//
// 2020 by Stefan Wagner (https://easyeda.com/wagiminator)
// License: http://creativecommons.org/licenses/by-sa/3.0/


//Libraries
#include <TinyI2CMaster.h>      // https://github.com/technoblogy/tiny-i2c
#include <Tiny4kOLED.h>         // https://github.com/datacute/Tiny4kOLED
#include <avr/pgmspace.h>       // for using data in program space
#include "font8x16a.h"          // reduced character set

// Pin definitions
#define SETBUTTON   3

// INA219 register values
#define INA_ADDR    0b01000000          // I2C address of INA219
#define INA_CONFIG  0b0000011001100111  // INA config register according to datasheet
#define INA_CALIB   5120                // INA calibration register according to R_SHUNT
#define CONFIG_REG  0x00                // INA configuration register address
#define CALIB_REG   0x05                // INA calibration register address
#define SHUNT_REG   0x01                // INA shunt voltage register address
#define VOLTAGE_REG 0x02                // INA bus voltage register address
#define POWER_REG   0x03                // INA power register address
#define CURRENT_REG 0x04                // INA current register address

// Conversions for the reduced character set
#define mV          ".,*"
#define mA          ".+*"
#define mW          ".-*"
#define mAh         ".+/"
#define mWh         ".-/"
#define SEP         "*)*"
#define SPACE       '*'
#define ZERO        '0'
#define DECIMAL     "("
#define AVERAGE     ";*"

// Variables (voltage in mV, current in mA, power in mW)
uint16_t  voltage, current, power;
uint16_t  minvoltage = 65535, maxvoltage = 0;
uint16_t  mincurrent = 65535, maxcurrent = 0;
uint16_t  minpower   = 65535, maxpower   = 0;
uint32_t  startmillis, lastmillis, nowmillis, interval, duration, seconds;
uint32_t  capacity = 0, energy = 0;
uint8_t   primescreen = 0;
bool      lastbutton  = true;


void setup() {
  // setup pins
  DDRB = 0;                             // all pins input now
  PORTB = bit (SETBUTTON);              // pullup for set button
  
  // start I2C
  TinyI2C.init();

  // start INA219
  initINA();

  // start OLED
  oled.begin();
  oled.setFont(FONT8X16A);
  oled.clear();
  oled.on();
  oled.switchRenderFrame();

  // init some variables
  startmillis = millis();
  lastmillis  = startmillis;
}

void loop() {
  // read voltage, current and power from INA219
  updateINA();

  // calculate power in mW
  power = (uint32_t)voltage * current / 1000;

  // calculate capacity in uAh and energy in uWh
  nowmillis   = millis();
  interval    = nowmillis - lastmillis;     // calculate time interval
  duration    = nowmillis - startmillis;    // calculate total duration
  seconds     = duration / 1000;            // calculate total seconds
  lastmillis  = nowmillis;
  capacity += interval * current / 3600;    // calculate uAh
  energy   += interval * power   / 3600;    // calculate uWh

  // update min/max values
  if (minvoltage > voltage) minvoltage = voltage;
  if (maxvoltage < voltage) maxvoltage = voltage;
  if (mincurrent > current) mincurrent = current;
  if (maxcurrent < current) maxcurrent = current;
  if (minpower   > power  ) minpower   = power;
  if (maxpower   < power  ) maxpower   = power;

  // check button
  if (bitRead(PINB, SETBUTTON)) lastbutton = false;
  else if (!lastbutton) {
    if (++primescreen > 4) primescreen = 0;
    lastbutton  = true;
  }

  // display values on the oled
  oled.clear();
  oled.setCursor(0, 0);
  switch (primescreen) {
    case 0:   printValue(voltage); oled.print(F(mV));
              printValue(power);   oled.print(F(mW));
              printValue(current); oled.print(F(mA));
              printValue(capacity / 1000); oled.print(F(mAh));
              break;
    case 1:   printValue(minvoltage); oled.print(F(SEP));
              printValue(maxvoltage); oled.print(F(mV));
              printValue(mincurrent); oled.print(F(SEP));
              printValue(maxcurrent); oled.print(F(mA));
              break;
    case 2:   oled.setCursor(0, 1);
              printValue(minpower);   oled.print(F(SEP));
              printValue(maxpower);   oled.print(F(mW));
              break;
    case 3:   printDigits(capacity / 1000, 7, SPACE); oled.print(F(DECIMAL));
              printDigits(capacity % 1000, 3, ZERO);  oled.print(F(mAh));
              oled.setCursor(0, 2);
              printDigits(energy / 1000, 7, SPACE); oled.print(F(DECIMAL));
              printDigits(energy % 1000, 3, ZERO);  oled.print(F(mWh));
              break;
    case 4:   oled.setCursor(32, 1);
              printDigits(seconds / 3600, 2, ZERO); oled.print(F(":"));
              seconds %= 3600;
              printDigits(seconds / 60,   2, ZERO); oled.print(F(":"));            
              printDigits(seconds % 60,   2, ZERO); 
              break;
    default:  break;
  } 
  oled.switchFrame();

  // a little delay
  delay(100);
}


// writes a register value to the INA219
void writeRegister(uint8_t reg, uint16_t value) {
  TinyI2C.start(INA_ADDR, 0);
  TinyI2C.write(reg);
  TinyI2C.write((value >> 8) & 0xff);
  TinyI2C.write(value & 0xff);
  TinyI2C.stop();
}

// reads a register from the INA219
uint16_t readRegister(uint8_t reg) {
  uint16_t result;
  TinyI2C.start(INA_ADDR, 0);
  TinyI2C.write(reg);
  TinyI2C.restart(INA_ADDR, 2);
  result = (uint16_t)(TinyI2C.read() << 8) | TinyI2C.read();
  TinyI2C.stop();
  return(result);
}

// writes inital configuration and calibration values to the INA
void initINA() {
  writeRegister(CONFIG_REG, INA_CONFIG);
  writeRegister(CALIB_REG,  INA_CALIB);
}

// read sensor values from INA219
void updateINA() {
  voltage = (readRegister(VOLTAGE_REG) >> 1) & 0xfffc;
  current = readRegister(CURRENT_REG);
  if (current > 32767) current = 0;
}

// prints 5-digit value right aligned
void printValue(uint16_t value) {
  printDigits(value, 5, SPACE);
}

// prints value right aligned
void printDigits(uint32_t value, uint8_t digits, char filler) {
  uint32_t counter = value;
  if (counter == 0) counter = 1;
  uint32_t limit   = 1;
  while (--digits) limit *= 10;
  while (counter < limit) {
    oled.print(filler);
    counter *= 10;
  }
  oled.print(value);
}
