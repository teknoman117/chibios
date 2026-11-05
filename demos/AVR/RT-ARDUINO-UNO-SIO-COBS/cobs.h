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
  COBS_NONE = 0,                /**< Frame in progress.                     */
  COBS_PACKET,                  /**< Valid packet, CRC verified.            */
  COBS_ERROR                    /**< Malformed, oversized or bad CRC.       */
} cobs_result_t;

/**
 * @brief   Incremental decoder, one byte at a time, suitable for ISRs.
 */
typedef struct {
  uint8_t       *buf;           /**< Destination buffer.                    */
  uint8_t       size;           /**< Destination buffer size.               */
  uint8_t       len;            /**< Decoded bytes, CRC included.           */
  uint8_t       block;          /**< Data bytes left in the current block.  */
  uint8_t       code;           /**< Current block code.                    */
  bool          started;        /**< A code byte has been received.         */
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
  void cobsDecoderInit(cobs_decoder_t *dp, uint8_t *buf, uint8_t size);
  void cobsDecoderReset(cobs_decoder_t *dp);
  cobs_result_t cobsDecodeByte(cobs_decoder_t *dp, uint8_t b);
  void cobsEncoderInit(cobs_encoder_t *ep, uint8_t *buf);
  void cobsEncodeByte(cobs_encoder_t *ep, uint8_t b);
  size_t cobsEncoderFinish(cobs_encoder_t *ep);
#ifdef __cplusplus
}
#endif

/**
 * @brief   Payload size of a decoded packet.
 */
#define cobsPayloadSize(dp)     ((uint8_t)((dp)->len - 2U))

#endif /* COBS_H */
