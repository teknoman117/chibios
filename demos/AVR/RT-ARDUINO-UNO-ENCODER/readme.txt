*****************************************************************************
** ChibiOS/RT port for Atmel AVR ATmega328p.                               **
*****************************************************************************

** TARGET **

The demo runs on an Arduino Uno board.

** The Demo **

The demo decodes a quadrature encoder with A, B and index outputs using the
EX GPIO encoder driver, a software X4 decoder built on the PAL pad event
callbacks. The encoder state is accessed through the generic EX
BaseEncoder interface.

Default pins, A and B on external interrupts:
- A on PD2 (pin 2 on Arduino IDE), INT0.
- B on PD3 (pin 3 on Arduino IDE), INT1.
Building with -DENCODER_USE_PCINT moves A and B to pin change interrupts:
- A on PC0 (pin A0 on Arduino IDE), PCINT8.
- B on PC1 (pin A1 on Arduino IDE), PCINT9.
The index is on PB0 (pin 8 on Arduino IDE), PCINT0, in both cases. A and B
have the internal pull-ups enabled.

Every 50ms the state is printed on Serial0, which is available on the board
USB connector, 115200 baud 8N1:

  P <position> I <index count> L <position at last index> E <errors>

The position is in quadrature counts, four per A/B cycle, and increases
when A leads B. Sending 'z' zeroes the position. Errors count the decoding
steps where both A and B changed, meaning that edges have been lost; the
LED on PB5 (pin 13 on Arduino IDE) is lit after the first error.

** Build Procedure **

The demo was built using the GCC AVR toolchain.
