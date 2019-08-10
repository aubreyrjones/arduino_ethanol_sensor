Arduino Ethanol Controller
--------------------------

This is Arduino software for an automotive ethanol fuel sensor controller.

The ethanol sensor is designed to give real-time readings of ethanol content
in fuel as a percentage between 0 and 100%. It outputs this as an FM signal
from 50-150Hz, with the ethanol content given by subtracting 50 from the
signal frequency. The sensor itself runs on 12V, but outputs a 5V signal.
It also outputs fuel temperature readings, which are not used in this
project.

When adding an ethanol sensor to a vehicle that does not already support flex
fuel, there is unlikely to be an ECU input natively compatible with the
sensor's FM output. Instead, only a 0-5V analog input will be available. This
project converts the sensor signal to a voltage that the ECU can accept.


# Features

* Converts 50-150Hz square wave FM to a 0.5-4.5V signal
* Voltages below 0.5V and above 4.5V are used for error states (can be used to set DTC)
* "Sane" ethanol values within milliseconds of power-on from persistent memory


# Design

Depends on 16MHz/5V Arduino. CPU frequency is assumed fixed.

This code assumes you are using an [I2C 12-bit DAC](https://www.sparkfun.com/products/12918) 
and a [I2C FRAM](https://www.adafruit.com/product/1895). Those links are to the breakout
boards used in prototyping this project's hardware. If you use those boards, be aware that
both of them have I2C pullup resistors. It is recommended that you desolder the pullup
resistors from the Adafruit board.

Electrical connection is simple. Wire the I2C lines in parallel from A4 and A5 of the Arduino.