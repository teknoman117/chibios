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

#include <util/crc16.h>

#include "cobs.h"

static void decoder_store(cobs_decoder_t *dp, uint8_t b) {

  if (dp->len < dp->size) {
    dp->buf[dp->len++] = b;
  }
  else {
    dp->error = true;
  }
}

/**
 * @brief   Initializes a decoder.
 *
 * @param[out] dp       decoder
 * @param[in] buf       destination buffer, payload plus 2 CRC bytes
 * @param[in] size      destination buffer size
 */
void cobsDecoderInit(cobs_decoder_t *dp, uint8_t *buf, uint8_t size) {

  dp->buf  = buf;
  dp->size = size;
  cobsDecoderReset(dp);
}

/**
 * @brief   Discards the frame in progress.
 * @note    Can be used to change the destination buffer between packets.
 *
 * @param[in,out] dp    decoder
 */
void cobsDecoderReset(cobs_decoder_t *dp) {

  dp->len     = 0U;
  dp->block   = 0U;
  dp->code    = 0xFFU;
  dp->started = false;
  dp->error   = false;
}

/**
 * @brief   Feeds one received byte to the decoder.
 * @note    After @p COBS_PACKET the buffer holds @p len bytes, the
 *          payload followed by the CRC. The decoder restarts by itself
 *          after a delimiter, the buffer can be changed then using
 *          @p cobsDecoderInit().
 *
 * @param[in,out] dp    decoder
 * @param[in] b         received byte
 * @return              The decoder state after this byte.
 */
cobs_result_t cobsDecodeByte(cobs_decoder_t *dp, uint8_t b) {
  cobs_result_t res;
  uint16_t crc;
  uint8_t i;

  if (b == 0U) {
    /* Delimiter, empty frames are idle fill and are ignored.*/
    if (!dp->started) {
      cobsDecoderReset(dp);
      return COBS_NONE;
    }

    /* A delimiter inside a block means a truncated frame.*/
    res = COBS_ERROR;
    if (!dp->error && (dp->block == 0U) && (dp->len >= 2U)) {
      crc = COBS_CRC_INIT;
      for (i = 0U; i < dp->len; i++) {
        crc = _crc_xmodem_update(crc, dp->buf[i]);
      }
      if (crc == 0U) {
        res = COBS_PACKET;
      }
    }

    /* Ready for the next frame, the length is kept for the caller.*/
    i = dp->len;
    cobsDecoderReset(dp);
    dp->len = i;
    return res;
  }

  if (!dp->started) {
    /* First byte of a new frame, the previous length is dropped.*/
    dp->len = 0U;
  }

  if (dp->block != 0U) {
    decoder_store(dp, b);
  }
  else {
    /* Code byte, the previous block implies a zero unless it was a
       maximum length block.*/
    if (dp->started && (dp->code != 0xFFU)) {
      decoder_store(dp, 0U);
    }
    dp->code    = b;
    dp->block   = b;
    dp->started = true;
  }
  dp->block--;

  return COBS_NONE;
}

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
