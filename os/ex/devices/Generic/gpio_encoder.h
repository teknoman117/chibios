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
 * @file    gpio_encoder.h
 * @brief   Quadrature encoder on GPIO pad events, header.
 * @details Generic software decoder for A/B/index encoders using the PAL
 *          pad event callbacks, any platform with @p PAL_USE_CALLBACKS
 *          can be used, on AVR either INTn or PCINTn pins.
 *
 * @addtogroup GPIO_ENCODER
 * @ingroup EX_GENERIC
 * @{
 */

#ifndef GPIO_ENCODER_H
#define GPIO_ENCODER_H

#include "ex_encoder.h"

/*===========================================================================*/
/* Driver constants.                                                         */
/*===========================================================================*/

/**
 * @name    Version identification
 * @{
 */
/**
 * @brief   GPIO encoder driver version string.
 */
#define EX_GPIO_ENCODER_VERSION             "1.0.0"

/**
 * @brief   GPIO encoder driver version major number.
 */
#define EX_GPIO_ENCODER_MAJOR               1

/**
 * @brief   GPIO encoder driver version minor number.
 */
#define EX_GPIO_ENCODER_MINOR               0

/**
 * @brief   GPIO encoder driver version patch number.
 */
#define EX_GPIO_ENCODER_PATCH               0
/** @} */

/*===========================================================================*/
/* Driver pre-compile time settings.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

#if PAL_USE_CALLBACKS != TRUE
#error "GPIO encoder requires PAL_USE_CALLBACKS"
#endif

/*===========================================================================*/
/* Driver data structures and types.                                         */
/*===========================================================================*/

/**
 * @brief   Index pulse handling.
 */
typedef enum {
  GPIO_ENCODER_INDEX_NONE = 0,      /**< No index line.                     */
  GPIO_ENCODER_INDEX_LATCH = 1,     /**< Position latched on index rise.    */
  GPIO_ENCODER_INDEX_RESET = 2      /**< Latched, then position zeroed.     */
} gpio_encoder_index_t;

/**
 * @brief   Driver state machine possible states.
 */
typedef enum {
  GPIO_ENCODER_UNINIT = 0,          /**< Not initialized.                   */
  GPIO_ENCODER_STOP = 1,            /**< Stopped.                           */
  GPIO_ENCODER_READY = 2            /**< Ready.                             */
} gpio_encoder_state_t;

/**
 * @brief   GPIO encoder configuration structure.
 * @note    The lines modes, pull-ups included, are configured by the
 *          application before starting the driver.
 */
typedef struct {
  /**
   * @brief   Channel A line.
   */
  ioline_t                  line_a;
  /**
   * @brief   Channel B line.
   */
  ioline_t                  line_b;
  /**
   * @brief   Index line, ignored if @p index is @p GPIO_ENCODER_INDEX_NONE.
   */
  ioline_t                  line_index;
  /**
   * @brief   Index pulse handling.
   */
  gpio_encoder_index_t      index;
  /**
   * @brief   Counting direction reversal.
   * @details With @p false the position increases when A leads B.
   */
  bool                      reverse;
} GPIOEncoderConfig;

/**
 * @brief   @p GPIOEncoderDriver specific methods.
 */
#define _gpio_encoder_methods_alone

/**
 * @brief   @p GPIOEncoderDriver specific methods with inherited ones.
 */
#define _gpio_encoder_methods                                               \
  _base_encoder_methods                                                     \
  _gpio_encoder_methods_alone

/**
 * @extends BaseEncoderVMT
 *
 * @brief   @p GPIOEncoderDriver virtual methods table.
 */
struct GPIOEncoderVMT {
  _gpio_encoder_methods
};

/**
 * @brief   @p GPIOEncoderDriver specific data.
 */
#define _gpio_encoder_data                                                  \
  _base_encoder_data                                                        \
  /* Driver state.*/                                                        \
  gpio_encoder_state_t      state;                                          \
  /* Current configuration data.*/                                          \
  const GPIOEncoderConfig   *config;                                        \
  /* Last decoded A/B levels, A in bit 1 and B in bit 0.*/                  \
  uint8_t                   ab;                                             \
  /* Decoder state, updated by the pad event callbacks.*/                   \
  encoder_state_t           enc;

/**
 * @extends BaseEncoder
 *
 * @brief   GPIO quadrature encoder driver class.
 */
typedef struct {
  /** @brief Virtual Methods Table.*/
  const struct GPIOEncoderVMT *vmt;
  _gpio_encoder_data
} GPIOEncoderDriver;

/*===========================================================================*/
/* Driver macros.                                                            */
/*===========================================================================*/

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif
  void gpioEncoderObjectInit(GPIOEncoderDriver *devp);
  msg_t gpioEncoderStart(GPIOEncoderDriver *devp,
                         const GPIOEncoderConfig *config);
  void gpioEncoderStop(GPIOEncoderDriver *devp);
#ifdef __cplusplus
}
#endif

#endif /* GPIO_ENCODER_H */

/** @} */
