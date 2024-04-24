/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: espnowTask.h
** ----------
** Header file for espnowTask.c. Provides constant
** values, function declarations, and FreeRTOS
** Semaphore/Mutex API declarations.
*/

#ifndef ESPNOWTASK_H_
#define ESPNOWTASK_H_

/* Standard Library Headers */
#include <stdio.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* Defines */
#define NOW_PRIO 24 /* Priority of Task */
/* NOTE:
** ESP-NOW has the highest priority on Core 0 since entering or exiting the 
** room safely should be prioritized over all else!
*/

/* Function Declarations */
extern void startEspnowConfig(void);

/* FreeRTOS Declared API Handles */
extern SemaphoreHandle_t xMtxEspnow;
extern SemaphoreHandle_t xSemEspnow;

#endif /* ESPNOWTASK_H_*/