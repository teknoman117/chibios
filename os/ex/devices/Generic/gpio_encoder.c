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
 * @file    gpio_encoder.c
 * @brief   Quadrature encoder on GPIO pad events, code.
 * @details Both edges of A and B are decoded (X4). Each pad event callback
 *          reads both lines and decodes the transition from the previous
 *          state. A change of both lines means that edges have been lost
 *          and the direction cannot be known, it is counted as a decoding
 *          error. Three or more edges within one interrupt latency can be
 *          miscounted without error, the maximum edge rate depends on the
 *          interrupt latency of the platform.
 *
 * @addtogroup GPIO_ENCODER
 * @ingroup EX_GENERIC
 * @{
 */

#include "hal.h"
#include "gpio_encoder.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

#define AB_A                    2U
#define AB_B                    1U

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/*
 * Transitions from the previous A/B state (row) to the new one (column),
 * A in bit 1 and B in bit 0. Counting forward A leads B:
 * 00->10->11->01->00. A two bits change cannot be decoded.
 */
#define TR_ERR                  2
static const int8_t transitions[4][4] = {
  /*           00       01       10       11   */
  /* 00 */ {   0,      -1,       1,  TR_ERR},
  /* 01 */ {   1,       0,  TR_ERR,      -1},
  /* 10 */ {  -1,  TR_ERR,       0,       1},
  /* 11 */ {TR_ERR,     1,      -1,       0}
};

/*
 * Decodes the current A/B levels, called by the A and B pad event
 * callbacks. Both lines are read: an edge already consumed by the other
 * line callback reads as no change.
 */
static void encoder_decode(GPIOEncoderDriver *devp) {
  uint8_t ab;
  int8_t tr;

  osalSysLockFromISR();
  ab = 0U;
  if (palReadLine(devp->config->line_a) == PAL_HIGH) {
    ab |= AB_A;
  }
  if (palReadLine(devp->config->line_b) == PAL_HIGH) {
    ab |= AB_B;
  }
  tr = transitions[devp->ab][ab];
  if (tr == TR_ERR) {
    devp->enc.errors++;
  }
  else if (devp->config->reverse) {
    devp->enc.position -= tr;
  }
  else {
    devp->enc.position += tr;
  }
  devp->ab = ab;
  osalSysUnlockFromISR();
}

/*
 * Channel A and B pad events, invoked from ISR context outside the kernel
 * lock.
 */
static void line_ab_cb(void *arg) {

  encoder_decode((GPIOEncoderDriver *)arg);
}

/*
 * Index pad event, rising edge, invoked from ISR context outside the
 * kernel lock.
 */
static void line_index_cb(void *arg) {
  GPIOEncoderDriver *devp = (GPIOEncoderDriver *)arg;

  osalSysLockFromISR();
  devp->enc.index_position = devp->enc.position;
  devp->enc.index_count++;
  if (devp->config->index == GPIO_ENCODER_INDEX_RESET) {
    devp->enc.position = 0;
  }
  osalSysUnlockFromISR();
}

/*===========================================================================*/
/* Interface implementation.                                                 */
/*===========================================================================*/

/*
 * Returns the position, callable from any context.
 */
static msg_t get_position(void *ip, int32_t *position) {
  GPIOEncoderDriver *devp = (GPIOEncoderDriver *)ip;
  syssts_t sts;

  osalDbgCheck((devp != NULL) && (position != NULL));
  osalDbgAssert(devp->state == GPIO_ENCODER_READY, "invalid state");

  sts = osalSysGetStatusAndLockX();
  *position = devp->enc.position;
  osalSysRestoreStatusX(sts);

  return MSG_OK;
}

/*
 * Sets the position, callable from any context.
 */
static msg_t set_position(void *ip, int32_t position) {
  GPIOEncoderDriver *devp = (GPIOEncoderDriver *)ip;
  syssts_t sts;

  osalDbgCheck(devp != NULL);
  osalDbgAssert(devp->state == GPIO_ENCODER_READY, "invalid state");

  sts = osalSysGetStatusAndLockX();
  devp->enc.position = position;
  osalSysRestoreStatusX(sts);

  return MSG_OK;
}

/*
 * Returns a consistent state snapshot, callable from any context.
 */
static msg_t read_state(void *ip, encoder_state_t *statep) {
  GPIOEncoderDriver *devp = (GPIOEncoderDriver *)ip;
  syssts_t sts;

  osalDbgCheck((devp != NULL) && (statep != NULL));
  osalDbgAssert(devp->state == GPIO_ENCODER_READY, "invalid state");

  sts = osalSysGetStatusAndLockX();
  *statep = devp->enc;
  osalSysRestoreStatusX(sts);

  return MSG_OK;
}

static const struct GPIOEncoderVMT vmt_device = {
  (size_t)0,
  get_position,
  set_position,
  read_state
};

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Initializes an instance.
 *
 * @param[out] devp     pointer to the @p GPIOEncoderDriver object
 *
 * @init
 */
void gpioEncoderObjectInit(GPIOEncoderDriver *devp) {

  devp->vmt    = &vmt_device;
  devp->state  = GPIO_ENCODER_STOP;
  devp->config = NULL;
}

/**
 * @brief   Starts decoding.
 * @details The position and the counters are cleared, the current A/B
 *          levels are taken as the starting point.
 * @note    The lines must already be configured as inputs.
 *
 * @param[in] devp      pointer to the @p GPIOEncoderDriver object
 * @param[in] config    pointer to the @p GPIOEncoderConfig object
 * @return              The operation status.
 * @retval MSG_OK       if the function succeeded.
 *
 * @api
 */
msg_t gpioEncoderStart(GPIOEncoderDriver *devp,
                       const GPIOEncoderConfig *config) {
  uint8_t ab;

  osalDbgCheck((devp != NULL) && (config != NULL));
  osalDbgAssert(devp->state == GPIO_ENCODER_STOP, "invalid state");

  ab = 0U;
  if (palReadLine(config->line_a) == PAL_HIGH) {
    ab |= AB_A;
  }
  if (palReadLine(config->line_b) == PAL_HIGH) {
    ab |= AB_B;
  }

  osalSysLock();
  devp->config             = config;
  devp->ab                 = ab;
  devp->enc.position       = 0;
  devp->enc.index_position = 0;
  devp->enc.index_count    = 0U;
  devp->enc.errors         = 0U;
  devp->state              = GPIO_ENCODER_READY;
  osalSysUnlock();

  palSetLineCallback(config->line_a, line_ab_cb, devp);
  palSetLineCallback(config->line_b, line_ab_cb, devp);
  palEnableLineEvent(config->line_a, PAL_EVENT_MODE_BOTH_EDGES);
  palEnableLineEvent(config->line_b, PAL_EVENT_MODE_BOTH_EDGES);
  if (config->index != GPIO_ENCODER_INDEX_NONE) {
    palSetLineCallback(config->line_index, line_index_cb, devp);
    palEnableLineEvent(config->line_index, PAL_EVENT_MODE_RISING_EDGE);
  }

  return MSG_OK;
}

/**
 * @brief   Stops decoding.
 *
 * @param[in] devp      pointer to the @p GPIOEncoderDriver object
 *
 * @api
 */
void gpioEncoderStop(GPIOEncoderDriver *devp) {
  const GPIOEncoderConfig *config;

  osalDbgCheck(devp != NULL);
  osalDbgAssert((devp->state == GPIO_ENCODER_STOP) ||
                (devp->state == GPIO_ENCODER_READY), "invalid state");

  if (devp->state == GPIO_ENCODER_READY) {
    config = devp->config;
    palDisableLineEvent(config->line_a);
    palDisableLineEvent(config->line_b);
    if (config->index != GPIO_ENCODER_INDEX_NONE) {
      palDisableLineEvent(config->line_index);
    }
  }
  devp->state = GPIO_ENCODER_STOP;
}

/** @} */
