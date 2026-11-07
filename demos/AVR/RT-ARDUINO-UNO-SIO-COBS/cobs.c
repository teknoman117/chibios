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

/**
 * @file    cobs.c
 * @brief   COBS framing with CRC-16/CCITT-FALSE.
 */

#include "cobs.h"

/**
 * @brief   Starts encoding a packet.
 *
 * @param[out] ep       encoder
 * @param[in] buf       output buffer, see @p COBS_ENCODED_SIZE()
 */
void cobsEncoderInit(cobs_encoder_t *ep, uint8_t *buf) {

  ep->start = buf;
  ep->codep = buf;
  ep->out   = buf + 1;
  ep->code  = 1U;
  ep->crc   = COBS_CRC_INIT;
}

static void encoder_stuff(cobs_encoder_t *ep, uint8_t b) {

  if (b != 0U) {
    *ep->out++ = b;
    ep->code++;
  }
  if ((b == 0U) || (ep->code == 0xFFU)) {
    *ep->codep = ep->code;
    ep->code   = 1U;
    ep->codep  = ep->out++;
  }
}

/**
 * @brief   Encodes one payload byte.
 *
 * @param[in,out] ep    encoder
 * @param[in] b         payload byte
 */
void cobsEncodeByte(cobs_encoder_t *ep, uint8_t b) {

  ep->crc = _crc_xmodem_update(ep->crc, b);
  encoder_stuff(ep, b);
}

/**
 * @brief   Appends the CRC and the delimiter.
 *
 * @param[in,out] ep    encoder
 * @return              The stuffed packet size, delimiter included.
 */
size_t cobsEncoderFinish(cobs_encoder_t *ep) {
  uint16_t crc = ep->crc;

  encoder_stuff(ep, (uint8_t)(crc >> 8));
  encoder_stuff(ep, (uint8_t)crc);
  *ep->codep = ep->code;
  *ep->out++ = 0U;

  return (size_t)(ep->out - ep->start);
}
