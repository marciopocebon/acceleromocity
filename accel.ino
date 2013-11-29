/*
 MMA8452Q Basic Example Code
 Nathan Seidle
 SparkFun Electronics
 November 5, 2012

 License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).

 This example code shows how to read the X/Y/Z accelerations and basic functions of the MMA5842. It leaves out
 all the neat features this IC is capable of (tap, orientation, and inerrupts) and just displays X/Y/Z. See
 the advanced example code to see more features.

 Hardware setup:
 MMA8452 Breakout ------------ Arduino
 3.3V --------------------- 3.3V
 SDA -------^^(330)^^------- A4
 SCL -------^^(330)^^------- A5
 GND ---------------------- GND

 The MMA8452 is 3.3V so we recommend using 330 or 1k resistors between a 5V Arduino and the MMA8452 breakout.

 The MMA8452 has built in pull-up resistors for I2C so you do not need additional pull-ups.
 */

#include <Wire.h> // Used for I2C
#include <Math.h>
#include <SoftPWM.h>

// The SparkFun breakout board defaults to 1, set to 0 if SA0 jumper on the bottom of the board is set
#define MMA8452_ADDRESS 0x1D  // 0x1D if SA0 is high, 0x1C if low

//Define a few of the registers that we will be accessing on the MMA8452
#define OUT_X_MSB 0x01
#define XYZ_DATA_CFG  0x0E
#define WHO_AM_I   0x0D
#define CTRL_REG1  0x2A

#define GSCALE 2 // Sets full-scale range to +/-2, 4, or 8g. Used to calc real g values.


unsigned long lastTime = millis();
unsigned int timeSinceLastCheck = 0;
float currentAcc[3] = {0.0, 0.0, 0.0};

static unsigned int ledCount = 8;
static unsigned int ledPins[8] = { 9, 2, 3, 4, 5, 6, 7, 8 };
static float ledsX[8] = {  0.0, -0.5, -1.0, -0.5,  0.5,  1.0,  0.5,  0.0 };
static float ledsY[8] = { -1.0, -0.5,  0.0,  0.5, -0.5,  0.0,  0.5,  1.0 };

bool flag = false;

void setup()
{
  Serial.begin(57600);
  Serial.println("MMA8452 Basic Example");

  // Enable interrupts on PCINT11 (pin 26) for tap interrupts
  PCMSK1 = 1<<PCINT11;

  // Enable interrupts PCINT14..8
  PCICR  = 1<<PCIE1;

  Wire.begin(); //Join the bus as a master

  initMMA8452(); //Test and intialize the MMA8452

  SoftPWMBegin();

  // Global interrupt enable
  sei();
}

void loop()
{
  int i, output;
  float dot;
  int accelCount[3];  // Stores the 12-bit signed value

  unsigned long currentTime = millis();
  unsigned long elapsedTime = currentTime - lastTime;
  lastTime = currentTime;
  timeSinceLastCheck += elapsedTime;

  if (timeSinceLastCheck > 100) {

    readAccelData(accelCount);  // Read the x/y/z adc values

    // Now we'll calculate the accleration value into actual g's
    float accelG[3];  // Stores the real accel value in g's
    for (i = 0; i < 3; i++) {
      accelG[i] = (float) accelCount[i] / ((1<<12)/(2*GSCALE));  // get actual g value, this depends on scale being set

      // use a rolling filter
      currentAcc[i] = 0.95 * accelG[i] + currentAcc[i] * 0.05;
    }

    if (flag) {
      return;
    }

    for (i = 0; i < ledCount; i++) {
      dot = currentAcc[0] * ledsX[i] + currentAcc[1] * ledsY[i];

      // invert dot so the up pointing led is lit
      output = int(255 * constrain(-dot, 0, 1));

      SoftPWMSet(ledPins[i], output);

      /*Serial.print(accelG[i], 2);  // Print g values*/
      /*Serial.print("/");*/
      /*Serial.print(acc, 2);*/
      /*Serial.print(output, HEX);  // Print color values*/
      /*Serial.print("\t");  // tabs in between axes*/

      /*Serial.println();*/
      timeSinceLastCheck = 0;
    }
  }
}

void readAccelData(int *destination)
{
  byte rawData[6];  // x/y/z accel register data stored here

  readRegisters(OUT_X_MSB, 6, rawData);  // Read the six raw data registers into data array

  // Loop to calculate 12-bit ADC and g value for each axis
  for(int i = 0; i < 3 ; i++)
  {
    int gCount = (rawData[i*2] << 8) | rawData[(i*2)+1];  //Combine the two 8 bit registers into one 12-bit number
    gCount >>= 4; //The registers are left align, here we right align the 12-bit integer

    // If the number is negative, we have to make it so manually (no 12-bit data type)
    if (rawData[i*2] > 0x7F)
    {
      gCount = ~gCount + 1;
      gCount *= -1;  // Transform into negative 2's complement #
    }

    destination[i] = gCount; //Record this gCount into the 3 int array
  }
}

// Initialize the MMA8452 registers
// See the many application notes for more info on setting all of these registers:
// http://www.freescale.com/webapp/sps/site/prod_summary.jsp?code=MMA8452Q
void initMMA8452()
{
  byte c = readRegister(WHO_AM_I);  // Read WHO_AM_I register
  if (c == 0x2A) // WHO_AM_I should always be 0x2A
  {
    Serial.println("MMA8452Q is online...");
  }
  else
  {
    Serial.print("Could not connect to MMA8452Q: 0x");
    Serial.println(c, HEX);
    while(1) ; // Loop forever if communication doesn't happen
  }

  MMA8452Standby();  // Must be in standby to change registers

  // Set up the full scale range to 2, 4, or 8g.
  byte fsr = GSCALE;
  if(fsr > 8) fsr = 8; //Easy error check
  fsr >>= 2; // Neat trick, see page 22. 00 = 2G, 01 = 4A, 10 = 8G
  writeRegister(XYZ_DATA_CFG, fsr);

  //The default data rate is 800Hz and we don't modify it in this example code


  /* Set up single and double tap - 5 steps:
   1. Set up single and/or double tap detection on each axis individually.
   2. Set the threshold - minimum required acceleration to cause a tap.
   3. Set the time limit - the maximum time that a tap can be above the threshold
   4. Set the pulse latency - the minimum required time between one pulse and the next
   5. Set the second pulse window - maximum allowed time between end of latency and start of second pulse
   for more info check out this app note: http://cache.freescale.com/files/sensors/doc/app_note/AN4072.pdf */
  writeRegister(0x21, 0x7F);  // 1. enable single/double taps on all axes
  // writeRegister(0x21, 0x55);  // 1. single taps only on all axes
  // writeRegister(0x21, 0x6A);  // 1. double taps only on all axes
  writeRegister(0x23, 0x20);  // 2. x thresh at 2g, multiply the value by 0.0625g/LSB to get the threshold
  writeRegister(0x24, 0x20);  // 2. y thresh at 2g, multiply the value by 0.0625g/LSB to get the threshold
  writeRegister(0x25, 0x08);  // 2. z thresh at .5g, multiply the value by 0.0625g/LSB to get the threshold
  writeRegister(0x26, 0x30);  // 3. 30ms time limit at 800Hz odr, this is very dependent on data rate, see the app note
  writeRegister(0x27, 0xA0);  // 4. 200ms (at 800Hz odr) between taps min, this also depends on the data rate
  writeRegister(0x28, 0xFF);  // 5. 318ms (max value) between taps max

  // Set up interrupt 1 and 2
  writeRegister(0x2C, 0x02);  // Active high, push-pull interrupts
  writeRegister(0x2D, 0x19);  // DRDY, P/L and tap ints enabled
  writeRegister(0x2E, 0x01);  // DRDY on INT1, P/L and taps on INT2

  MMA8452Active();  // Set to active to start reading
}

// Sets the MMA8452 to standby mode. It must be in standby to change most register settings
void MMA8452Standby()
{
  byte c = readRegister(CTRL_REG1);
  writeRegister(CTRL_REG1, c & ~(0x01)); //Clear the active bit to go into standby
}

// Sets the MMA8452 to active mode. Needs to be in this mode to output data
void MMA8452Active()
{
  byte c = readRegister(CTRL_REG1);
  writeRegister(CTRL_REG1, c | 0x01); //Set the active bit to begin detection
}

// Read bytesToRead sequentially, starting at addressToRead into the dest byte array
void readRegisters(byte addressToRead, int bytesToRead, byte * dest)
{
  Wire.beginTransmission(MMA8452_ADDRESS);
  Wire.write(addressToRead);
  Wire.endTransmission(false); //endTransmission but keep the connection active

  Wire.requestFrom(MMA8452_ADDRESS, bytesToRead); //Ask for bytes, once done, bus is released by default

  while(Wire.available() < bytesToRead); //Hang out until we get the # of bytes we expect

  for(int x = 0 ; x < bytesToRead ; x++)
    dest[x] = Wire.read();
}

// Read a single byte from addressToRead and return it as a byte
byte readRegister(byte addressToRead)
{
  Wire.beginTransmission(MMA8452_ADDRESS);
  Wire.write(addressToRead);
  Wire.endTransmission(false); //endTransmission but keep the connection active

  Wire.requestFrom(MMA8452_ADDRESS, 1); //Ask for 1 byte, once done, bus is released by default

  while(!Wire.available()) ; //Wait for the data to come back
  return Wire.read(); //Return this one byte
}

// Writes a single byte (dataToWrite) into addressToWrite
void writeRegister(byte addressToWrite, byte dataToWrite)
{
  Wire.beginTransmission(MMA8452_ADDRESS);
  Wire.write(addressToWrite);
  Wire.write(dataToWrite);
  Wire.endTransmission(); //Stop transmitting
}

// interrupts
ISR(PCINT1_vect) {
  int i;

  /* byte source = readRegister(0x22);  // Reads the PULSE_SRC register */

  flag = !flag;

  /* if ((source & 0x08)==0x08)  // If DPE (double puls) bit is set */
  /*   flag = true; */
  /* else */
  /*   flag = false; */

  for (i = 0; i < ledCount; i++) {
    SoftPWMSet(ledPins[i], 255);
  }
}
