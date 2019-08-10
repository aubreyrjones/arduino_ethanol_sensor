#include <Arduino.h>
#include <Wire.h>
#include <avr/builtins.h>
#include <avr/cpufunc.h>

#include <Adafruit_FRAM_I2C.h>


#define ETH_SERIAL_DEBUG

#define IN_PIN 8

// DAC chip
const byte MCP4725_ADDR = 0x60;
const byte DAC_WRITE_IMMEDIATE_COMMAND = 0x40;
const byte DAC_CODE_WIDTH = 12;  // 12-bit DAC

// voltage limits and ranges
#define MIN_ETH_VOLTAGE 0.5f
#define MAX_ETH_VOLTAGE 4.5f
#define MAX_VOLTAGE 5
const uint16_t vStep = (1 << DAC_CODE_WIDTH) / MAX_VOLTAGE;
const float ethVRange = MAX_ETH_VOLTAGE - MIN_ETH_VOLTAGE;
const float ethVStep = ethVRange / 100.0f;

// ethanol and controller state
int8_t ethanolPercentage = 0;

volatile bool framConnected = false;
volatile byte sensorErrorCount = 0;
volatile uint16_t sensorFrequency = 0;

// DAC functions
inline float clamp(float low, float high, float x) {
  if (low > x) return low;
  if (high < x) return high;
  return x;
}

uint16_t voltageToDAC(float v) {
  uint16_t dacCode = clamp(0, MAX_VOLTAGE, v) * vStep;
  
  #ifdef ETH_SERIAL_DEBUG
  Serial.print('v'); Serial.print(v); Serial.print('d'); Serial.println(dacCode);
  #endif
  
  return dacCode;
}

void dacOutImmediate(uint16_t dacCode) {
  Wire.beginTransmission(MCP4725_ADDR);
  Wire.write(DAC_WRITE_IMMEDIATE_COMMAND);
  Wire.write((byte) (dacCode >> 4));        // the 8 most significant bits...
  Wire.write((byte) ((dacCode & 15) << 4)); // the 4 least significant bits...
  Wire.endTransmission();
}

// ethanol sensor frequency capture
void setupTimer()   // setup timer1
{
  TCCR1A = 0;      // normal mode
  TCCR1B = 132;    // (10000100) Falling edge trigger, Timer = CPU Clock/256, noise cancellation on
  TCCR1C = 0;      // normal mode
  TIMSK1 = 33;     // (00100001) Input capture and overflow interupts enabled
  TCNT1 = 0;       // start from 0
}

ISR(TIMER1_CAPT_vect)    // PULSE DETECTED!  (interrupt automatically triggered, not called by main program)
{
  uint16_t revTick = ICR1; // save duration of last revolution
  TCNT1 = 0;       // restart timer for next revolution
  
  if (revTick > 6500 || revTick < 250) { // 10-250 Hz "valid" range
    ++sensorErrorCount;
  }
  else {
    sensorErrorCount = 0;
    sensorFrequency = 62200 / revTick;
    return;
  }
}

ISR(TIMER1_OVF_vect)
{
  ++sensorErrorCount;
}


// === FRAM ====
Adafruit_FRAM_I2C fram = Adafruit_FRAM_I2C();

void persistToFRAM(int8_t value) {
  fram.write8(23, value);
}

int8_t fetchFromFRAM() {
  return fram.read8(23);
}

// == ethanol and filtering ==
float ethanolToVoltage(int8_t eth) {
  return eth * ethVStep + MIN_ETH_VOLTAGE;
}

bool updateEthanol() {
  uint16_t freqSample = sensorFrequency; // local copy of volatile

  if (freqSample >= 50 && freqSample <= 150) {
    ethanolPercentage = freqSample - 50;

    return true;
  }

  return false;
}

void outputEthanol() {
  dacOutImmediate(voltageToDAC(ethanolToVoltage(ethanolPercentage)));
}

void setup() {
  #ifdef ETH_SERIAL_DEBUG
  Serial.begin(115200);
  #endif

  pinMode(IN_PIN, INPUT);
  setupTimer();

  Wire.begin();
  Wire.setClock(400000);

  if (fram.begin()) {
    framConnected = true;

    int8_t savedEth = fetchFromFRAM();
    if (savedEth >= 0 && savedEth <= 100) {
      ethanolPercentage = savedEth;
      outputEthanol(); // drive the saved value immediately since it appears valid.
    }
  }
}

void loop() {
  static bool sensorErrorState = false;
  static long lastSampleTime = 0;

  if (!sensorErrorCount) { // we've got good sync
    sensorErrorState = false;

    if (updateEthanol()) { // the ethanol has been updated from the sensor
      lastSampleTime = millis(); // save the time (good for 50 days continuous)
      if (framConnected) {
        persistToFRAM(ethanolPercentage); // store this last reading to the FRAM 
      }
    }
  }
  else if ((millis() - lastSampleTime) > 2000) {
    // we've had no valid ethanol sensor readings for 2 seconds
    sensorErrorState = true;
  }
  
  if (sensorErrorState) {
    // output max voltage for the error
    dacOutImmediate(4095);

    #ifdef ETH_SERIAL_DEBUG
    Serial.println('e');
    #endif
  }
  else {
    outputEthanol();
  }

  #ifdef ETH_SERIAL_DEBUG
  Serial.println(ethanolPercentage);
  #endif
  
  delay(100);
}
