/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: ledTask.c
** --------
** Configures LED pins and GPIO ISR for release button. 
** Creates a task to send to toggle the LEDs accordingly. Creates
** an ISR callback function to handle sending an ESP-NOW message if
** the release button is pressed.
*/

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
#include "esp_log.h"
#include "esp_err.h"

/* Local Headers */
#include "main.h"
#include "espnowTask.h"
#include "ledTask.h"



/* Variable Naming Abbreviations Legend:
**
** Sem - Semaphore
** Mtx - Mutex
** Rtrn - Return
** Len - Length
** G_* - Green
** R_* - Red
** INTR - Interrupt
**
*/



/* Local Function Declarations */
static void xBlinkTask(void *pvParameters);
static void startLedRtosConfig(void);

/* FreeRTOS Local API Handles */
static SemaphoreHandle_t xSemLed;

/* Static Constant Logging String */
static const char TAG[TAG_LEN_8] = "ESP_LED";



/* The releaseButtonISR() function is a callback function
** for a GPIO ISR. This ISR occurs upon the press of the 
** release button for the locking mechanism.
**
** Parameters:
**  none used
**
** Return:
**  none
** 
** Notes: The Pseudo-Mutex (Counting Semaphore) that guards
** the xEspnowTask() function is attempted to be taken here.
** If it is not available, then it is exited. If it is available,
** then it disables its own ISR and gives a synchronization
** Semaphore also for the xEspnowTask(). The reason for the Pseudo-
** Mutex is to prevent the possible race condition that may occur
** between the touchscreen (when a correct code is entered) and the release button
** when it comes to sending an ESP-NOW message to unlock the door. In this way, only
** one can occur and that is whichever one takes the Mutex first. 
*/
static void IRAM_ATTR releaseButtonISR(void *arg)
{
    if(xSemaphoreTakeFromISR(xMtxEspnow, pdFALSE))
    {
        gpio_intr_disable(INTR_PIN);
        xSemaphoreGiveFromISR(xSemEspnow, pdFALSE);
    }
}



/* The startPinConfig() function is used to initialize the pins
** for the LED in addition to the pin for the GPIO ISR (release button).
**
** Parameters:
**  none
**
** Return:
**  none
*/
void startPinConfig(void)
{
    static bool initialized = false;

    /* To prevent double initialization */
    if (initialized)
    {
        return;
    }
    else
    {
        initialized = true;
    }

    gpio_config_t pinConfig;

    /* LED_MASK bits allow for initalization of both LED pins */
    pinConfig.pin_bit_mask = LED_MASK;
    pinConfig.mode = GPIO_MODE_OUTPUT;
    pinConfig.pull_up_en = 0;
    pinConfig.pull_down_en = 0;
    pinConfig.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&pinConfig);
    
    pinConfig.pin_bit_mask = INTR_MASK;
    pinConfig.mode = GPIO_MODE_INPUT;
    pinConfig.pull_down_en = 1;
    pinConfig.intr_type = GPIO_INTR_POSEDGE;
    gpio_config(&pinConfig);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(INTR_PIN, releaseButtonISR, NULL);

    startLedRtosConfig();
}



/* The startLedRtosConfig() function is used to initialize the
** single Binary Semaphore that is used by the xBlinkTask and its
** associated functions. The xBlinkTask() is also created here.
**
** Parameters:
**  none
**
** Return:
**  none
*/
static void startLedRtosConfig(void)
{
    if(!(xSemLed = xSemaphoreCreateBinary()))
    {
        ESP_LOGW(TAG, "%s xSemLed%s", heapFail, rtrnNewLine);
    }

    xTaskCreatePinnedToCore(&xBlinkTask, "LED_TASK", (STACK_DEPTH / 2), 0, CORE1_PRIO, 0, 1);
}



/* The giveSemLed() function simply gives the
** synchronizing Semaphore xSemLed. This function 
** was created for the sake of abstraction and to prevent
** the need to share global variables. 
**
** Parameters:
**  none
**
** Return:
**  none
*/
void giveSemLed(void)
{
    xSemaphoreGive(xSemLed);
}



/* The xBlinkTask() function turns the red LED on
** before pending indefinitely on the xSemLed synchronization
** Semaphore. Once the Semaphore can be taken, the red LED
** is turned off and then green LED is turned on for a period
** of time before switching back to the original state and 
** pending indefinitely once again.
**
** Parameters:
**  none
**
** Return:
**  none
*/
static void xBlinkTask(void *pvParameters)
{
    while(true)
    {
        gpio_set_level(R_LED_PIN, SET_HIGH);
        xSemaphoreTake(xSemLed, portMAX_DELAY);

        gpio_set_level(R_LED_PIN, SET_LOW);
        gpio_set_level(G_LED_PIN, SET_HIGH);

        vTaskDelay(LOCK_DELAY);

        gpio_set_level(G_LED_PIN, SET_LOW);
    }
}