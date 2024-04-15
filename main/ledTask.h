/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: ledTask.h
** ----------
** ......
*/

#ifndef LEDTASK_H_
#define LEDTASK_H_

/* Standard Library Headers */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* Driver Headers */
#include "driver/gpio.h"

/* Defines */
#define INTR_MASK (1ULL << GPIO_NUM_41)
#define R_LED_MASK (1ULL << GPIO_NUM_38)
#define G_LED_MASK (1ULL << GPIO_NUM_39)

#define INTR_PIN GPIO_NUM_41
#define R_LED_PIN GPIO_NUM_38
#define G_LED_PIN GPIO_NUM_39

#define SET_HIGH 1
#define SET_LOW 0

#define ESP_INTR_FLAG_DEFAULT 0

/* Function Declarations */
extern void startPinConfig(void);
extern void giveSemLed(void);

#endif /* LEDTASK_H_*/