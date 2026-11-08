/*
    ChibiOS - Copyright (C) 2006-2026 Giovanni Di Sirio.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "ch.h"
#include "hal.h"
#include "chprintf.h"

#include "hal_gpio_qencoder.h"

/*
 * Encoder lines, A and B on INT0/INT1 by default or on PCINT1 pins, index
 * always on a PCINT0 pin.
 */
#if !defined(ENCODER_USE_PCINT)
#define LINE_ENC_A              PAL_LINE(IOPORT4, 2U)   /* PD2, pin 2.      */
#define LINE_ENC_B              PAL_LINE(IOPORT4, 3U)   /* PD3, pin 3.      */
#define ENC_PINS                "A=D2(INT0) B=D3(INT1)"
#else
#define LINE_ENC_A              PAL_LINE(IOPORT3, 0U)   /* PC0, pin A0.     */
#define LINE_ENC_B              PAL_LINE(IOPORT3, 1U)   /* PC1, pin A1.     */
#define ENC_PINS                "A=A0(PCINT8) B=A1(PCINT9)"
#endif
#define LINE_ENC_INDEX          PAL_LINE(IOPORT2, 0U)   /* PB0, pin 8.      */

static GPIOQEncoderDriver encoder;

/* Filled at runtime, on AVR PAL_LINE() is not a constant expression.*/
static GPIOQEncoderConfig encoder_cfg;

/*
 * Application entry point.
 */
int main(void) {
  BaseSequentialStream *chp = (BaseSequentialStream *)&SD1;
  BaseEncoder *encp = (BaseEncoder *)&encoder;
  systime_t next;

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  /*
   * Activates the serial driver 1 using the driver default configuration.
   * PD0(RX) and PD1(TX) are routed to USART0.
   */
  sdStart(&SD1, NULL);
  chprintf(chp, "\r\nencoder demo " ENC_PINS " I=D8(PCINT0)\r\n");

  /*
   * Lines setup then decoding start.
   */
  palSetLineMode(LINE_LED1, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(LINE_ENC_A, PAL_MODE_INPUT_PULLUP);
  palSetLineMode(LINE_ENC_B, PAL_MODE_INPUT_PULLUP);
  palSetLineMode(LINE_ENC_INDEX, PAL_MODE_INPUT);
  encoder_cfg.line_a     = LINE_ENC_A;
  encoder_cfg.line_b     = LINE_ENC_B;
  encoder_cfg.line_index = LINE_ENC_INDEX;
  encoder_cfg.index      = GPIO_QENCODER_INDEX_LATCH;
  encoder_cfg.reverse    = false;
  gpioQEncoderObjectInit(&encoder);
  (void) gpioQEncoderStart(&encoder, &encoder_cfg);

  /*
   * State report every 50ms, 'z' zeroes the position. The LED is lit after
   * decoding errors.
   */
  next = chVTGetSystemTime();
  while (true) {
    encoder_state_t st;
    msg_t c;

    while ((c = chnGetTimeout(&SD1, TIME_IMMEDIATE)) >= 0) {
      if (c == 'z') {
        (void) encoderSetPosition(encp, 0);
      }
    }

    (void) encoderReadState(encp, &st);
    chprintf(chp, "P %ld I %u L %ld E %u\r\n", (long)st.position,
             st.index_count, (long)st.index_position, st.errors);
    if (st.errors != 0U) {
      palSetLine(LINE_LED1);
    }

    next = chTimeAddX(next, TIME_MS2I(50));
    chThdSleepUntil(next);
  }
}
