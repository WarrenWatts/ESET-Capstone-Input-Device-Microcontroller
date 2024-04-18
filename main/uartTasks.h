/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: uartTasks.h
** ----------
** ......
*/

#ifndef UARTTASKS_H_
#define UARTTASKS_H_

/* Standard Library Headers */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* Driver Headers */
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"

/* Other Headers */
#include "cJSON.h"



/* Defines */
#define READ_DELAY pdMS_TO_TICKS(100)

/* Function Declarations */
extern void startUartConfig(void);
extern bool getTimeBool(void);
int32_t getAccessCode(void);

/* Typedefs for Pointer to Function and Function */
typedef char* (*printingFuncPtr)(cJSON *responsePtr);
typedef char* (printingFunc)(cJSON *responsePtr);

#endif /* UARTTASKS_H_ */