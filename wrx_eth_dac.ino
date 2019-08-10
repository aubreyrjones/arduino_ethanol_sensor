#include <Arduino.h>
#include <Wire.h>
#include <avr/builtins.h>
#include <avr/cpufunc.h>

#include <Adafruit_FRAM_I2C.h>

#define ETH_SERIAL_DEBUG

constexpr byte IN_PIN = 8;

// DAC chip
constexpr byte MCP4725_ADDR = 0x60;
constexpr byte DAC_WRITE_IMMEDIATE_COMMAND = 0x40;
constexpr byte DAC_CODE_WIDTH = 12;  // 12-bit DAC
constexpr uint16_t maxDAC = (1 << DAC_CODE_WIDTH);

// voltage limits and ranges
constexpr float MIN_ETH_VOLTAGE = 0.5f;
constexpr float MAX_ETH_VOLTAGE = 4.5f;
constexpr float MAX_VOLTAGE = 5.0f;

constexpr uint16_t vStep = round(maxDAC / MAX_VOLTAGE);

constexpr inline float clamp(float const& low, float const& high, float const& x) {
  return (low > x) ? low :
          (high < x) ? high : x;
}

constexpr inline uint16_t voltageToDAC(float const& v) {
  return round(clamp(0, MAX_VOLTAGE, v) * vStep);
}

constexpr uint16_t minEthDAC = voltageToDAC(MIN_ETH_VOLTAGE);
constexpr uint16_t ethVRange = voltageToDAC(MAX_ETH_VOLTAGE) - voltageToDAC(MIN_ETH_VOLTAGE);
constexpr uint16_t ethVStep = round(ethVRange / 100.0f);
constexpr uint16_t sensorErrorVoltage = voltageToDAC(5.0f);
constexpr uint16_t sensorNeverConnectedVoltage = voltageToDAC(0.10f);

// ethanol and controller state
int8_t ethanolPercentage = 0;

volatile bool framConnected = false;
volatile byte sensorErrorCount = 0;
volatile uint16_t sensorFrequency = 0;

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

// === DAC ===
void dacOutImmediate(uint16_t const& dacCode) {
  Wire.beginTransmission(MCP4725_ADDR);
  Wire.write(DAC_WRITE_IMMEDIATE_COMMAND);
  Wire.write((byte) (dacCode >> 4));        // the 8 most significant bits...
  Wire.write((byte) ((dacCode & 15) << 4)); // the 4 least significant bits...
  Wire.endTransmission();

  #ifdef ETH_SERIAL_DEBUG
  Serial.print('d');
  Serial.println(dacCode);
  #endif
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
constexpr float ethanolToDAC(int8_t const& eth) {
  return eth * ethVStep + minEthDAC;
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
  dacOutImmediate(ethanolToDAC(ethanolPercentage));
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
  static unsigned long lastSampleTime = 0;

  if (!sensorErrorCount) { // we've got good sync, or we haven't overflowed yet
    sensorErrorState = false;

    if (updateEthanol()) { // the ethanol has been updated from the sensor
      lastSampleTime = millis(); // save the time (good for 50 days continuous)
      if (framConnected) {
        persistToFRAM(ethanolPercentage); // store this last reading to the FRAM 
      }
    }
  }
  else if ((!framConnected && !lastSampleTime) || // no sync, never sync'd, and no FRAM
            ((millis() - lastSampleTime) > 2000)) { // or we've spent 2 seconds with no sensor
    sensorErrorState = true; // immediate error
  }
  
  if (!sensorErrorState) {
    outputEthanol();
    
    #ifdef ETH_SERIAL_DEBUG
    Serial.println(ethanolPercentage);
    Serial.print('f'); Serial.println(sensorFrequency);
    #endif
  }
  else {
    uint16_t errorVoltage;
    
    #ifdef ETH_SERIAL_DEBUG
    char errorChar;
    #endif

    if (lastSampleTime) {
      errorVoltage = sensorErrorVoltage;
      
      #ifdef ETH_SERIAL_DEBUG
      errorChar = 's';
      #endif
    }
    else {
      if (framConnected) {
        errorVoltage = sensorNeverConnectedVoltage;
      }
      else {
        errorVoltage = 0;
      }
      
      #ifdef ETH_SERIAL_DEBUG
      errorChar = 'c';
      #endif
    }

    dacOutImmediate(errorVoltage);

    #ifdef ETH_SERIAL_DEBUG
    Serial.println('n');
    #endif
  }
  
  delay(100);
}
