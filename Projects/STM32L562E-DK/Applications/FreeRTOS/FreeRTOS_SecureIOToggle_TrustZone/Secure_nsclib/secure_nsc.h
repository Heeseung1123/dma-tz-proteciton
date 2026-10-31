/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Secure_nsclib/secure_nsc.h
  * @author  MCD Application Team
  * @brief   Header for secure non-secure callable APIs list
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* USER CODE BEGIN Non_Secure_CallLib_h */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef SECURE_NSC_H
#define SECURE_NSC_H

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
/**
  * @brief  non-secure callback ID enumeration definition
  */
typedef enum
{
  SECURE_FAULT_CB_ID     = 0x00U, /*!< System secure fault callback ID */
  GTZC_ERROR_CB_ID       = 0x01U  /*!< GTZC secure error callback ID */
} SECURE_CallbackIDTypeDef;

#if 0 
 typedef enum {
     MPCBB_OK                = 0,
     MPCBB_ERR_NOT_ALIGNED   = -1,  // Base or Length is not 256 Byte aligned
     MPCBB_ERR_OUT_OF_RANGE  = -2,  // buffer is out of SRAM
     MPCBB_ERR_NO_LOWER_ROOM = -3,  // buffer is top, no room for Top Guard
     MPCBB_ERR_NO_UPPER_ROOM = -4,  // buffer is bottom, no room for Bottom Guard
 } mpcbb_status_t;
#endif 

/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
void SECURE_RegisterCallback(SECURE_CallbackIDTypeDef CallbackId, void *func);
void SECURE_LEDToggle(void);

//int SECURE_guard_buffer(uint32_t, uint32_t);
//
int SECURE_dma_m2m_start(void*, void*, uint32_t);
int SECURE_dma_m2m_busy(void);
int SECURE_dma_m2m_init(void*); 
int SECURE_nop3(void*, void*, uint32_t);
#endif /* SECURE_NSC_H */
/* USER CODE END Non_Secure_CallLib_h */
