/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: wifiTask.c
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
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

/* Local Headers */
#include "wifiTask.h"
#include "main.h"



/* Local Function Declarations */
static void xWifiTask(void *pvParameters);
static void startWifiRtosConfig(void);
static void setConnectCnt(uint8_t newCount);
static uint8_t getConnectCnt(void);
static void wifiConnectHndlr(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void wifiDisconnectHndlr(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

/* FreeRTOS Local API Handles */
static SemaphoreHandle_t xSemWifiStatus;
static SemaphoreHandle_t xSemDisconnect;
static SemaphoreHandle_t xMtxConnectCnt;

/* Reference Declarations of Global Constant Strings */
extern const char heapFail[HEAP_LEN];
extern const char mtxFail[MTX_LEN];
extern const char rtrnNewLine[NEWLINE_LEN];

/* Local Constant Logging String */
static const char TAG[TAG_LEN_9] = "ESP_WIFI";

/* Counter of Wi-Fi Connection Fails */
static uint8_t failConnectCnt = 0;



/* The startWifiConfig() function...
**
** Parameters:
**  none
**
** Return:
**  none
*/
void startWifiConfig(void)
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

    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifiInit = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifiInit));

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, wifiConnectHndlr, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifiDisconnectHndlr, NULL));

    esp_netif_create_default_wifi_sta();
    wifi_config_t wifiStaConfig = 
    {
        .sta = 
        {
            .ssid = SSID,
            .password = PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifiStaConfig));

    esp_netif_create_default_wifi_ap();
    wifi_config_t wifiApConfig =
    {
        .ap =
        {
            .channel = DFLT_CHNL,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifiApConfig));

    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_channel(DFLT_CHNL, WIFI_SECOND_CHAN_NONE);

    startWifiRtosConfig();

    xTaskCreatePinnedToCore(&xWifiTask, "WIFI_TASK", STACK_DEPTH, 0, WIFI_PRIO, 0, 0);
}



/* The startWifiRtosConfig() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
void startWifiRtosConfig(void)
{
    if(!(xSemWifiStatus = xSemaphoreCreateCounting(PSEUDO_MTX_CNT, PSEUDO_MTX_CNT)))
    {
        ESP_LOGE(TAG, "%s xSemWifiStatus%s", heapFail, rtrnNewLine);
    }

    if(!(xSemDisconnect = xSemaphoreCreateBinary()))
    {
        ESP_LOGE(TAG, "%s xSemDisconnect%s", heapFail, rtrnNewLine);
    }

    if(!(xMtxConnectCnt = xSemaphoreCreateMutex()))
    {
        ESP_LOGE(TAG, "%s xMtxConnectCnt%s", heapFail, rtrnNewLine);
    }
}



/* The wifiConnectHndlr() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
static void wifiConnectHndlr(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    xSemaphoreTake(xSemWifiStatus, DEF_PEND);

    setConnectCnt(0);

    ESP_LOGI(TAG, "Connected%s", rtrnNewLine);
}



/* The wifiDisconnectHndlr() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
static void wifiDisconnectHndlr(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t localFailCount = 0;

    ESP_LOGI(TAG, "Disconnected%s", rtrnNewLine);

    if((localFailCount = getConnectCnt()) == 0)
    {
        xSemaphoreGive(xSemWifiStatus);
    }

    if(localFailCount < MAX_ATMPT)
    {
        setConnectCnt(++localFailCount);
    }

    xSemaphoreGive(xSemDisconnect);
}



/* The wifiCheckStatus() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
UBaseType_t wifiCheckStatus(void)
{
    return uxSemaphoreGetCount(xSemWifiStatus);
}



/* The getConnectCnt() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
static uint8_t getConnectCnt(void)
{
    uint8_t localFailCount = 0;

    if(xSemaphoreTake(xMtxConnectCnt, DEF_PEND))
    {
        localFailCount = failConnectCnt;
        xSemaphoreGive(xMtxConnectCnt);
    }
    else
    {
        ESP_LOGE(TAG, "xMtxConnectCnt %s in getConnectCnt()%s", mtxFail, rtrnNewLine);
    }

    return localFailCount;
}



/* The setConnectCnt() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
static void setConnectCnt(uint8_t newCount)
{
    if(xSemaphoreTake(xMtxConnectCnt, DEF_PEND))
    {
        failConnectCnt = newCount;
        xSemaphoreGive(xMtxConnectCnt);
    }
    else
    {
        ESP_LOGE(TAG, "xMtxConnectCnt %s in setConnectCnt()%s", mtxFail, rtrnNewLine);
    }
}



/* The xWifiTask() function...
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes:
*/
static void xWifiTask(void *pvParameters)
{
    uint16_t reconnectDelay;

    esp_wifi_connect();

    while(true)
    {
        xSemaphoreTake(xSemDisconnect, portMAX_DELAY);

        reconnectDelay = (getConnectCnt() > MAX_ATMPT) ? FIRST_11_DELAY : DEF_DELAY;

        vTaskDelay(reconnectDelay);

        esp_wifi_connect();
    }
}