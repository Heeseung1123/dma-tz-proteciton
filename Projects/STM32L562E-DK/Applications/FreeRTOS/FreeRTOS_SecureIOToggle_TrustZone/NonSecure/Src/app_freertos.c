/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
     
/* USER CODE END Application */

/* Example data the task is allowed to see */
static uint8_t ucPrivateBuffer[256];
static const uint32_t ulLookupTable[128];
/* Task stack. Alignment is important for the LED port. */
#define STACK_SIZE_WORDS 256
static StackType_t xLEDTaskStack[ STACK_SIZE_WORDS ] __attribute__((aligned(32)));

/* Simple unprivileged task */
static void vLEDTestTask( void *pvParameters )
{
  uint8_t i = 0;
  (void)pvParameters;
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);

  for( ;; )
  {
	  SECURE_LEDToggle();
	  vTaskDelay(pdMS_TO_TICKS(200));
    /* Can read/write ucPrivateBuffer */
//    ucPrivateBuffer[i] = (uint8_t)i;
    /* Can read ulLookupTable but not write it */
//    uint32_t v = ulLookupTable[i % 128];
//    (void)v;
    /* Any access outside configured regions will trigger MemManage fault */
//    i++;
  }
}

/* Parameters for xTaskCreateRestricted */
static const TaskParameters_t xLEDTestTaskParams =
{
  .pvTaskCode = vLEDTestTask,
  .pcName = "LED_Task",
  .usStackDepth = STACK_SIZE_WORDS,
  .pvParameters = NULL,
  /* Normal priority, unprivileged (no portPRIVILEGE_BIT) */
  .uxPriority =  (tskIDLE_PRIORITY + 1 ),
  /* Pre-allocated stack buffer */
  .puxStackBuffer = xLEDTaskStack,
  /* LED regions visible to this task */
  .xRegions =
  {
    /* Region 0: read/write data buffer */
    { ucPrivateBuffer,sizeof( ucPrivateBuffer ),portMPU_REGION_READ_WRITE },
      /* Region 1: read only lookup table in flash */
      { ulLookupTable,sizeof( ulLookupTable ),portMPU_REGION_READ_ONLY },
      /* Region 2: unused */
      { 0, 0, 0 }
  }
};

void vCreateAppTasks( void )
{
  /* Create the task as unprivileged with the LED regions above. */
  xTaskCreateRestricted( &xLEDTestTaskParams, NULL );
}
