/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: espnowTask.h
** ----------
** ......
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
#define NOW_PRIO 23

/* Function Declarations */
extern void startEspnowConfig(void);

/* FreeRTOS API Handle Declarations */
extern SemaphoreHandle_t xMtxEspnow;
extern SemaphoreHandle_t xSemEspnow;

#endif /* ESPNOWTASK_H_*/