/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: xEspnowTask.c
** --------
** .......
*/

/* Standard Library Headers */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* Driver Headers */
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_err.h"
#include "esp_log.h"

/* Local Headers */
#include "ledTask.h"
#include "wifiTask.h"
#include "main.h"
#include "espnowTask.h"

/* NOTE: !!!!!!!!!!!!!!!!!! RENAME VARIABLES !!!!!!!!!!!!!!!!!! */

/* Local Defines */
#define MSG_LEN 2

/* Local Function Declarations */
static void espnowRtosConfig(void);
static void espnowPeerConfig(void);
static void xEspnowTask(void *pvParameters);
void espNowRcvCallback(const esp_now_recv_info_t* espNowInfo, const uint8_t *myData, int dateLen);

/* FreeRTOS Local API Handles */
static SemaphoreHandle_t xMtxRcvInfo;

/* FreeRTOS Defining API Handles */
SemaphoreHandle_t xMtxEspnow;
SemaphoreHandle_t xSemEspnow;

/* ESPNOW Peer Handle */
static esp_now_peer_info_t receiverInfo;

/* Reference Declarations of Global Constant Strings */
extern const char rtrnNewLine[NEWLINE_LEN];
extern const char heapFail[HEAP_LEN];
extern const char mtxFail[MTX_LEN];

/* Local Constant Logging String */
static const char TAG[TAG_LEN_8] = "ESP_NOW";

/* Peer Receiver MAC Address String */
static const uint8_t receiverMAC[ESP_NOW_ETH_ALEN] = {0x7C, 0xDF, 0xA1, 0xE5, 0x44, 0x30};



/* The startEspnowConfig() function...
**
** Parameters:
**  none
**
** Return:
**  none
*/
void startEspnowConfig(void)
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

    ESP_ERROR_CHECK(esp_now_init());
    esp_now_register_recv_cb(espNowRcvCallback);
    espnowRtosConfig();
    espnowPeerConfig();
}



/* */
void espNowRcvCallback(const esp_now_recv_info_t* espNowInfo, const uint8_t *myData, int dateLen)
{
    const uint8_t msgData[MSG_LEN] = "2";
    if(esp_now_send(receiverMAC, msgData, MSG_LEN) == ESP_ERR_ESPNOW_ARG)
    {
        espnowPeerConfig();
    }
}



/* The espnowRtosConfig() function...
**
** Parameters:
**  none
**
** Return:
**  none
*/
static void espnowRtosConfig(void)
{
    if(!(xMtxRcvInfo = xSemaphoreCreateMutex()))
    {
        ESP_LOGE(TAG, "%s xMtxRcvInfo%s", heapFail, rtrnNewLine);
    }

    if(!(xMtxEspnow = xSemaphoreCreateCounting(PSEUDO_MTX_CNT, PSEUDO_MTX_CNT)))
    {
        ESP_LOGE(TAG, "%s xMtxEspnow%s", heapFail, rtrnNewLine);
    }

    if(!(xSemEspnow = xSemaphoreCreateBinary()))
    {
        ESP_LOGE(TAG, "%s xSemEspnow%s", heapFail, rtrnNewLine);
    }

    xTaskCreatePinnedToCore(&xEspnowTask, "ESPNOW_TASk", STACK_DEPTH, 0, NOW_PRIO, 0, 0);
}



/* The espnowPeerConfig() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
static void espnowPeerConfig(void)
{
    if(xSemaphoreTake(xMtxRcvInfo, DEF_PEND))
    {
        static bool peerInitialized = false;

        uint8_t primaryChnl = 0;
        wifi_second_chan_t secondaryChnl = 0;

        esp_wifi_get_channel(&primaryChnl, &secondaryChnl);
        receiverInfo.channel = primaryChnl;

        if(!peerInitialized)
        {
            peerInitialized = true;

            receiverInfo.ifidx = ESP_IF_WIFI_STA;
            receiverInfo.encrypt = false;
            memcpy(receiverInfo.peer_addr, receiverMAC, ESP_NOW_ETH_ALEN);

            esp_now_add_peer(&receiverInfo);
        }
        else
        {
            esp_now_mod_peer(&receiverInfo);
        }
        xSemaphoreGive(xMtxRcvInfo);
    }
    else
    {
        ESP_LOGW(TAG, "xMtxRcvInfo %s%s", mtxFail, rtrnNewLine);
    }
}



/* The xEspnowTask() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
static void xEspnowTask(void *pvParameters)
{
    while(true)
    {
        xSemaphoreTake(xSemEspnow, portMAX_DELAY);

        uint8_t count;
        int16_t result = 0;
        const uint8_t msgData[MSG_LEN] = "1";

        for(count = 0; count < MAX_ATMPT; count++)
        {
            if((result = esp_now_send(receiverMAC, msgData, MSG_LEN)) == ESP_OK)
            {
                break;
            }

            ESP_LOGE(TAG, "Error No: %d%s", result, rtrnNewLine);

            if(result == ESP_ERR_ESPNOW_ARG)
            {
                espnowPeerConfig();
            }
            
            vTaskDelay(FIRST_11_DELAY);
        }
        gpio_intr_enable(INTR_PIN);
        xSemaphoreGive(xMtxEspnow);
    }
}