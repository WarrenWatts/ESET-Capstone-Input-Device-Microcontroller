/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: ledTask.c
** --------
** .......
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



/* Local Defines */
#define TAG_SZ 8
#define TOGGLE_CNT 2

/* Static Function Declarations */
static void xBlinkTask(void *pvParameters);
static void startLedRtosConfig(void);

/* FreeRTOS API Handles */
static SemaphoreHandle_t xSemLed;

/* FreeRTOS API Reference Handles */
extern SemaphoreHandle_t xMtxEspnow;
extern SemaphoreHandle_t xSemEspnow;

/* Static Constant Logging String */
static const char TAG[TAG_SZ] = "ESP_LED";



static void IRAM_ATTR releaseButtonISR(void *arg)
{
    if(xSemaphoreTakeFromISR(xMtxEspnow, pdFALSE))
    {
        gpio_intr_disable(INTR_PIN);
        xSemaphoreGiveFromISR(xSemEspnow, pdFALSE);
    }
}



/* The startPinConfig() function...
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

    pinConfig.pin_bit_mask = R_LED_MASK;
    pinConfig.mode = GPIO_MODE_OUTPUT;
    pinConfig.pull_up_en = 0;
    pinConfig.pull_down_en = 0;
    pinConfig.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&pinConfig);

    pinConfig.pin_bit_mask = G_LED_MASK;
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



static void startLedRtosConfig(void)
{
    if(!(xSemLed = xSemaphoreCreateBinary()))
    {
        ESP_LOGW(TAG, "%s Semaphore%s", heapFail, rtrnNewLine);
    }

    xTaskCreatePinnedToCore(&xBlinkTask, "LED_TASK", 2048, 0, CORE1_PRIO, 0, 1); //FIXME (MAGIC NUMBER)
}



void giveSemLed(void)
{
    xSemaphoreGive(xSemLed);
}



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