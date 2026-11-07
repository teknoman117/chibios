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

#include "cobs.h"

/*===========================================================================*/
/* Protocol.                                                                 */
/*===========================================================================*/

/* Device to host.*/
#define PKT_TELEMETRY           0x01U   /* Seq, uptime ms, good, bad, drop  */
#define PKT_CREDIT              0x04U   /* Credit limit, u16 LE             */
#define PKT_ECHO_REPLY          0x82U   /* Echoed payload                   */
#define PKT_PERIOD_REPLY        0x83U   /* New period ms                    */
#define PKT_STALL_REPLY         0x85U   /* Stall time ms                    */
#define PKT_NAK                 0xFFU   /* Rejected packet type             */

/* Host to device.*/
#define PKT_ECHO                0x02U   /* Any payload                      */
#define PKT_SET_PERIOD          0x03U   /* Period ms, u16 LE, 10..10000     */
#define PKT_STALL               0x05U   /* Stall ms, u16 LE, 0..1000        */

/* Packet type plus payload, CRC excluded.*/
#define PKT_MAX_SIZE            40U

/*===========================================================================*/
/* RX path, COBS decoding into a ring buffer in the SIO callback.            */
/*===========================================================================*/

/*
 * The ring holds the received packets as a length byte followed by the
 * packet. The indices are 8 bits wide, the ring size is 256 so they wrap
 * by themselves, one slot is kept free to tell full from empty.
 *
 * - rb_head, written by the ISR, next byte of the frame being decoded.
 * - rb_mark, written by the ISR, start of the frame being decoded, it is
 *   also the end of the last complete packet.
 * - rb_tail, written by the application, start of the oldest unread
 *   packet.
 *
 * A frame is decoded in place after a reserved length slot at rb_mark,
 * on a valid CRC the length is written, the two CRC bytes are dropped and
 * rb_mark is moved to rb_head. A frame with errors is discarded moving
 * rb_head back to rb_mark.
 */
#define RB_CAPACITY             255U

static uint8_t rb[256];
static volatile uint8_t rb_head;
static uint8_t rb_mark;
static volatile uint8_t rb_tail;

/* Received bytes counter, credits are given in received bytes.*/
static volatile uint16_t rx_bytes;

/* Signaled once for each complete packet in the ring.*/
static SEMAPHORE_DECL(rx_sem, 0);

static cobs_decoder_t decoder;
static bool rx_overflow;

static volatile uint16_t rx_good;
static volatile uint16_t rx_bad;
static volatile uint16_t rx_drop;

/*
 * Stores a decoded byte in the frame being decoded.
 */
static inline void rx_store(uint8_t data) {
  uint8_t h = rb_head;

  if (decoder.error) {
    return;
  }

  /* The first byte of a frame also takes the length slot.*/
  if (decoder.len == 1U) {
    if ((uint8_t)(rb_mark + 1U) == rb_tail) {
      decoder.error = true;
      rx_overflow   = true;
      return;
    }
    h = (uint8_t)(rb_mark + 1U);
  }

  if (decoder.len > (PKT_MAX_SIZE + 2U)) {
    /* Oversized frame.*/
    decoder.error = true;
  }
  else if ((uint8_t)(h + 1U) == rb_tail) {
    /* Ring full, the frame is dropped.*/
    decoder.error = true;
    rx_overflow   = true;
  }
  else {
    rb[h]   = data;
    rb_head = (uint8_t)(h + 1U);
  }
}

/*
 * SIO callback, invoked from the USART ISRs outside the kernel lock. All
 * the received bytes are drained and decoded here, complete packets with a
 * valid CRC are committed to the ring and the application is signaled.
 */
static void sio_cb(SIODriver *siop) {

  while (!sioIsRXEmptyX(siop)) {
    cobs_result_t res;
    uint8_t b, data;

    b = (uint8_t)sioGetX(siop);
    rx_bytes++;
    if (sioHasRXErrorsX(siop)) {
      /* Framing, parity or overrun error, the frame is discarded.*/
      (void) sioGetAndClearErrorsX(siop);
      decoder.error = true;
    }

    res = cobsDecodeByte(&decoder, b, &data);
    if (res == COBS_DATA) {
      rx_store(data);
    }
    else if (res == COBS_PACKET) {
      uint8_t end = (uint8_t)(rb_head - 2U);

      /* Length slot written, CRC dropped, packet committed.*/
      rb[rb_mark] = (uint8_t)(decoder.len - 2U);
      rb_head     = end;
      rb_mark     = end;
      rx_good++;
      palToggleLine(LINE_LED1);
      chSysLockFromISR();
      chSemSignalI(&rx_sem);
      chSysUnlockFromISR();
    }
    else if (res == COBS_ERROR) {
      rb_head = rb_mark;
      if (rx_overflow) {
        rx_drop++;
      }
      else {
        rx_bad++;
      }
      rx_overflow = false;
    }
  }
}

/*
 * Copies the oldest packet out of the ring and releases its space.
 * Must be called once for each rx_sem signal.
 */
static uint8_t packet_get(uint8_t *buf) {
  uint8_t t = rb_tail;
  uint8_t n, i;

  n = rb[t++];
  for (i = 0U; i < n; i++) {
    buf[i] = rb[t++];
  }
  rb_tail = t;

  return n;
}

/*===========================================================================*/
/* TX path.                                                                  */
/*===========================================================================*/

static MUTEX_DECL(tx_mtx);
static uint8_t tx_buffer[COBS_ENCODED_SIZE(PKT_MAX_SIZE)];

static void put_u16(uint8_t *p, uint16_t v) {

  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

/* Must be called with tx_mtx locked.*/
static void packet_send_locked(uint8_t type, const uint8_t *payload,
                               uint8_t n) {
  cobs_encoder_t enc;
  size_t len;

  cobsEncoderInit(&enc, tx_buffer);
  cobsEncodeByte(&enc, type);
  while (n-- > 0U) {
    cobsEncodeByte(&enc, *payload++);
  }
  len = cobsEncoderFinish(&enc);
  (void) chnWrite((BaseChannel *)&SIOD0, tx_buffer, len);
}

static void packet_send(uint8_t type, const uint8_t *payload, uint8_t n) {

  chMtxLock(&tx_mtx);
  packet_send_locked(type, payload, n);
  chMtxUnlock(&tx_mtx);
}

/*===========================================================================*/
/* Flow control.                                                             */
/*===========================================================================*/

/*
 * The host may send bytes while its count of sent bytes is lower than the
 * last credit limit received. The limit is the count of the bytes received
 * so far plus the free ring space, each received byte takes at most one
 * ring byte so the host can never overflow the ring. The limit is absolute,
 * lost credit packets, noise and discarded frames do not affect it.
 */
#define CREDIT_STEP             64U

static uint16_t credit_sent;

static uint16_t credit_limit(void) {
  uint16_t limit;

  chSysLock();
  limit = (uint16_t)(rx_bytes + RB_CAPACITY - (uint8_t)(rb_head - rb_tail));
  chSysUnlock();

  return limit;
}

/*
 * Sends the credit limit if it advanced by at least step bytes, a zero
 * step forces the report.
 */
static void credit_report(uint16_t step) {
  uint16_t limit;
  uint8_t pkt[2];

  chMtxLock(&tx_mtx);
  limit = credit_limit();
  if ((step == 0U) || ((uint16_t)(limit - credit_sent) >= step)) {
    credit_sent = limit;
    put_u16(pkt, limit);
    packet_send_locked(PKT_CREDIT, pkt, (uint8_t)sizeof pkt);
  }
  chMtxUnlock(&tx_mtx);
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

    /* Periodic refresh, it also returns the space of discarded frames.*/
    credit_report(1U);

    next = chTimeAddX(next, TIME_MS2I(period));
    chThdSleepUntil(next);
  }
}

/*===========================================================================*/
/* Application entry point.                                                  */
/*===========================================================================*/

int main(void) {

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
  cobsDecoderInit(&decoder);

  /*
   * Activates the SIO driver 0 using the driver default configuration.
   * PD0(RX) and PD1(TX) are routed to USART0.
   */
  sioSetCallbackX(&SIOD0, sio_cb);
  (void) sioStart(&SIOD0, NULL);

  /* Initial credit, the whole ring.*/
  credit_report(0U);

  chThdCreateStatic(waTelemetry, sizeof(waTelemetry), NORMALPRIO + 1,
                    Telemetry, NULL);

  /*
   * Packets processing.
   */
  while (true) {
    uint8_t pkt[PKT_MAX_SIZE];
    uint8_t n, type;
    bool pending;

    (void) chSemWait(&rx_sem);
    n = packet_get(pkt);
    type = pkt[0];

    if (n == 0U) {
      /* Packets without type are just ignored.*/
    }
    else if (type == PKT_ECHO) {
      packet_send(PKT_ECHO_REPLY, &pkt[1], (uint8_t)(n - 1U));
    }
    else if ((type == PKT_SET_PERIOD) && (n == 3U)) {
      uint16_t period = (uint16_t)(pkt[1] | (pkt[2] << 8));
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
    else if ((type == PKT_STALL) && (n == 3U)) {
      /* Simulated slow processing, the ring fills meanwhile.*/
      uint16_t ms = (uint16_t)(pkt[1] | (pkt[2] << 8));

      if (ms > 1000U) {
        ms = 1000U;
      }
      chThdSleepMilliseconds(ms);
      packet_send(PKT_STALL_REPLY, &pkt[1], 2U);
    }
    else {
      packet_send(PKT_NAK, &type, 1U);
    }

    /* Space returned to the host, in batches while packets are pending.*/
    chSysLock();
    pending = chSemGetCounterI(&rx_sem) > (cnt_t)0;
    chSysUnlock();
    credit_report(pending ? CREDIT_STEP : 1U);
  }
}
