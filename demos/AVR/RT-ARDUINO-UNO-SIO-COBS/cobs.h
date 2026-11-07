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
 * @file    cobs.h
 * @brief   COBS framing with CRC-16/CCITT-FALSE.
 * @details Packet layout before stuffing: payload, CRC high, CRC low.
 *          The CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, not
 *          reflected) computed by the avr-libc @p _crc_xmodem_update(),
 *          the CRC of a whole valid packet including its CRC is zero.
 *          Each stuffed packet is terminated by a 0x00 delimiter.
 */

#ifndef COBS_H
#define COBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <util/crc16.h>

/**
 * @brief   CRC-16/CCITT-FALSE initial value.
 */
#define COBS_CRC_INIT           0xFFFFU

/**
 * @brief   Worst case stuffed size of a packet of @p n payload bytes,
 *          CRC and delimiter included.
 */
#define COBS_ENCODED_SIZE(n)    ((n) + 2U + (((n) + 2U) / 254U) + 2U)

/**
 * @brief   Decoder results.
 */
typedef enum {
  COBS_NONE = 0,                /**< No decoded byte.                       */
  COBS_DATA,                    /**< One decoded byte returned.             */
  COBS_PACKET,                  /**< Frame end, CRC verified.               */
  COBS_ERROR                    /**< Frame end, malformed or bad CRC.       */
} cobs_result_t;

/**
 * @brief   Incremental decoder, one byte at a time, suitable for ISRs.
 * @details The decoder does not store data, each decoded byte is returned
 *          to the caller. The CRC is updated as the bytes are decoded so
 *          the frame end check does not depend on the frame size.
 */
typedef struct {
  uint16_t      crc;            /**< Running CRC of the decoded bytes.      */
  uint8_t       len;            /**< Decoded bytes, CRC included.           */
  uint8_t       block;          /**< Data bytes left in the current block.  */
  uint8_t       code;           /**< Current block code.                    */
  bool          started;        /**< A frame is in progress.                */
  bool          error;          /**< Frame to be discarded.                 */
} cobs_decoder_t;

/**
 * @brief   Incremental encoder computing the CRC on the fly.
 */
typedef struct {
  uint8_t       *start;         /**< Start of the output buffer.            */
  uint8_t       *out;           /**< Next output position.                  */
  uint8_t       *codep;         /**< Pending code byte position.            */
  uint8_t       code;           /**< Pending code value.                    */
  uint16_t      crc;            /**< Running CRC.                           */
} cobs_encoder_t;

#ifdef __cplusplus
extern "C" {
#endif
  void cobsEncoderInit(cobs_encoder_t *ep, uint8_t *buf);
  void cobsEncodeByte(cobs_encoder_t *ep, uint8_t b);
  size_t cobsEncoderFinish(cobs_encoder_t *ep);
#ifdef __cplusplus
}
#endif

/**
 * @brief   Initializes a decoder.
 *
 * @param[out] dp       decoder
 */
static inline void cobsDecoderInit(cobs_decoder_t *dp) {

  dp->started = false;
  dp->error   = false;
  dp->len     = 0U;
}

/**
 * @brief   Feeds one received byte to the decoder.
 * @note    At most one byte is decoded for each received byte: the zero
 *          implied by a block is returned when the next code byte is
 *          received, the last block of a frame has no implied zero. After
 *          @p COBS_PACKET or @p COBS_ERROR the @p len field holds the
 *          decoded size of the frame, CRC included.
 *
 * @param[in,out] dp    decoder
 * @param[in] b         received byte
 * @param[out] outp     decoded byte, valid on @p COBS_DATA
 * @return              The decoder result for this byte.
 */
static inline cobs_result_t cobsDecodeByte(cobs_decoder_t *dp, uint8_t b,
                                           uint8_t *outp) {
  uint8_t data;

  if (b == 0U) {
    bool valid;

    /* Delimiter, empty frames are idle fill and are ignored.*/
    if (!dp->started) {
      return COBS_NONE;
    }

    /* A delimiter inside a block means a truncated frame.*/
    valid = !dp->error && (dp->block == 0U) && (dp->len >= 2U) &&
            (dp->crc == 0U);
    dp->started = false;
    return valid ? COBS_PACKET : COBS_ERROR;
  }

  if (!dp->started) {
    /* First code byte of a frame.*/
    dp->started = true;
    dp->error   = false;
    dp->len     = 0U;
    dp->crc     = COBS_CRC_INIT;
    dp->code    = b;
    dp->block   = (uint8_t)(b - 1U);
    return COBS_NONE;
  }

  if (dp->block != 0U) {
    dp->block--;
    data = b;
  }
  else {
    /* Code byte, the previous block implies a zero unless it was a
       maximum length block.*/
    bool zero = dp->code != 0xFFU;

    dp->code  = b;
    dp->block = (uint8_t)(b - 1U);
    if (!zero) {
      return COBS_NONE;
    }
    data = 0U;
  }

  dp->len++;
  dp->crc = _crc_xmodem_update(dp->crc, data);
  *outp = data;
  return COBS_DATA;
}

#endif /* COBS_H */
