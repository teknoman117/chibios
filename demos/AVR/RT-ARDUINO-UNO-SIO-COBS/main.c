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

#include <string.h>

#include "ch.h"
#include "hal.h"

#include "cobs.h"

/*===========================================================================*/
/* Protocol.                                                                 */
/*===========================================================================*/

/* Device to host.*/
#define PKT_TELEMETRY           0x01U   /* Seq, uptime ms, good, bad, drop  */
#define PKT_ECHO_REPLY          0x82U   /* Echoed payload                   */
#define PKT_PERIOD_REPLY        0x83U   /* New period ms                    */
#define PKT_NAK                 0xFFU   /* Rejected packet type             */

/* Host to device.*/
#define PKT_ECHO                0x02U   /* Any payload                      */
#define PKT_SET_PERIOD          0x03U   /* Period ms, u16 LE, 10..10000     */

/* Packet type plus payload, CRC excluded.*/
#define PKT_MAX_SIZE            40U

/*===========================================================================*/
/* RX path, COBS decoding and CRC check in the SIO interrupt callback.       */
/*===========================================================================*/

#define RX_BUFFERS              5U

typedef struct {
  uint8_t       len;
  uint8_t       data[PKT_MAX_SIZE + 2U];
} rx_buffer_t;

static rx_buffer_t rx_buffers[RX_BUFFERS];
static msg_t rx_free_msgs[RX_BUFFERS];
static msg_t rx_full_msgs[RX_BUFFERS];
static MAILBOX_DECL(rx_free, rx_free_msgs, RX_BUFFERS);
static MAILBOX_DECL(rx_full, rx_full_msgs, RX_BUFFERS);

static cobs_decoder_t decoder;
static rx_buffer_t *rx_current;

static volatile uint16_t rx_good;
static volatile uint16_t rx_bad;
static volatile uint16_t rx_drop;

/*
 * SIO callback, invoked from the USART ISRs outside the kernel lock. All
 * the received bytes are drained and decoded here, complete packets with a
 * valid CRC are posted to the thread.
 */
static void sio_cb(SIODriver *siop) {

  while (!sioIsRXEmptyX(siop)) {
    cobs_result_t res;
    uint8_t b;

    b = (uint8_t)sioGetX(siop);
    if (sioHasRXErrorsX(siop)) {
      /* Framing, parity or overrun error, the frame is discarded.*/
      (void) sioGetAndClearErrorsX(siop);
      decoder.error = true;
    }

    res = cobsDecodeByte(&decoder, b);
    if (res == COBS_PACKET) {
      msg_t msg;

      rx_current->len = cobsPayloadSize(&decoder);
      chSysLockFromISR();
      if (chMBFetchI(&rx_free, &msg) == MSG_OK) {
        (void) chMBPostI(&rx_full, (msg_t)rx_current);
        rx_current = (rx_buffer_t *)msg;
        rx_good++;
        palToggleLine(LINE_LED1);
      }
      else {
        /* No free buffers, the packet is dropped and the buffer reused.*/
        rx_drop++;
      }
      chSysUnlockFromISR();
      cobsDecoderInit(&decoder, rx_current->data,
                      (uint8_t)sizeof (rx_current->data));
    }
    else if (res == COBS_ERROR) {
      rx_bad++;
    }
  }
}

/*===========================================================================*/
/* TX path.                                                                  */
/*===========================================================================*/

static MUTEX_DECL(tx_mtx);
static uint8_t tx_buffer[COBS_ENCODED_SIZE(PKT_MAX_SIZE)];

static void packet_send(uint8_t type, const uint8_t *payload, uint8_t n) {
  cobs_encoder_t enc;
  size_t len;

  chMtxLock(&tx_mtx);
  cobsEncoderInit(&enc, tx_buffer);
  cobsEncodeByte(&enc, type);
  while (n-- > 0U) {
    cobsEncodeByte(&enc, *payload++);
  }
  len = cobsEncoderFinish(&enc);
  (void) chnWrite((BaseChannel *)&SIOD0, tx_buffer, len);
  chMtxUnlock(&tx_mtx);
}

static void put_u16(uint8_t *p, uint16_t v) {

  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

/*===========================================================================*/
/* Telemetry thread.                                                         */
/*===========================================================================*/

static uint16_t telemetry_ms = 100U;

static THD_WORKING_AREA(waTelemetry, 128);
static THD_FUNCTION(Telemetry, arg) {
  uint8_t pkt[11];
  uint8_t seq = 0U;
  uint32_t ms;
  uint16_t period;
  systime_t next;

  (void)arg;
  chRegSetThreadName("telemetry");

  next = chVTGetSystemTime();
  while (true) {
    syssts_t sts;

    ms = (uint32_t)TIME_I2MS(chVTGetSystemTime());
    pkt[0] = seq++;
    pkt[1] = (uint8_t)ms;
    pkt[2] = (uint8_t)(ms >> 8);
    pkt[3] = (uint8_t)(ms >> 16);
    pkt[4] = (uint8_t)(ms >> 24);
    sts = chSysGetStatusAndLockX();
    put_u16(&pkt[5], rx_good);
    put_u16(&pkt[7], rx_bad);
    put_u16(&pkt[9], rx_drop);
    period = telemetry_ms;
    chSysRestoreStatusX(sts);
    packet_send(PKT_TELEMETRY, pkt, (uint8_t)sizeof pkt);

    next = chTimeAddX(next, TIME_MS2I(period));
    chThdSleepUntil(next);
  }
}

/*===========================================================================*/
/* Application entry point.                                                  */
/*===========================================================================*/

int main(void) {
  unsigned i;

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  palSetLineMode(LINE_LED1, PAL_MODE_OUTPUT_PUSHPULL);

  /*
   * RX buffers, one is owned by the decoder, the others are free.
   */
  rx_current = &rx_buffers[0];
  for (i = 1U; i < RX_BUFFERS; i++) {
    (void) chMBPostTimeout(&rx_free, (msg_t)&rx_buffers[i], TIME_IMMEDIATE);
  }
  cobsDecoderInit(&decoder, rx_current->data,
                  (uint8_t)sizeof (rx_current->data));

  /*
   * Activates the SIO driver 0 using the driver default configuration.
   * PD0(RX) and PD1(TX) are routed to USART0.
   */
  sioSetCallbackX(&SIOD0, sio_cb);
  (void) sioStart(&SIOD0, NULL);

  chThdCreateStatic(waTelemetry, sizeof(waTelemetry), NORMALPRIO + 1,
                    Telemetry, NULL);

  /*
   * Packets processing.
   */
  while (true) {
    rx_buffer_t *rxp;
    msg_t msg;
    uint8_t type;

    (void) chMBFetchTimeout(&rx_full, &msg, TIME_INFINITE);
    rxp = (rx_buffer_t *)msg;
    type = rxp->data[0];

    if (rxp->len == 0U) {
      /* Packets without type are just ignored.*/
    }
    else if (type == PKT_ECHO) {
      packet_send(PKT_ECHO_REPLY, &rxp->data[1], (uint8_t)(rxp->len - 1U));
    }
    else if ((type == PKT_SET_PERIOD) && (rxp->len == 3U)) {
      uint16_t period = (uint16_t)(rxp->data[1] | (rxp->data[2] << 8));
      uint8_t reply[2];

      chSysLock();
      if ((period >= 10U) && (period <= 10000U)) {
        telemetry_ms = period;
      }
      period = telemetry_ms;
      chSysUnlock();
      put_u16(reply, period);
      packet_send(PKT_PERIOD_REPLY, reply, (uint8_t)sizeof reply);
    }
    else {
      packet_send(PKT_NAK, &type, 1U);
    }

    (void) chMBPostTimeout(&rx_free, msg, TIME_INFINITE);
  }
}
