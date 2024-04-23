/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: wifiTask.c
** --------
** Configures Wi-Fi functionality for the ESP device 
** and creates a task that controls the connection and
** reconnection to Wi-Fi.
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



/* Variable Naming Abbreviations Legend:
**
** Sem - Semaphore
** Mtx - Mutex
** Rtrn - Return
** Len - Length
** Cnt - Count
** Atmpt - Attempt
** Def - Default
**
*/



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



/* The startWifiConfig() function is used to initialize Wi-Fi
** functionality for this ESP device. This Wi-Fi configuration
** will utilize the 'Station + Soft AP' Mode. 
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes: When using the 'Station + Soft AP' Mode for the ESP,
** remember that the channel of the Station and the channel
** of the Soft AP must ALWAYS be the same!
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

    /* Non-volatile Storage Initalization*/
    ESP_ERROR_CHECK(nvs_flash_init());

    /* TCP/IP Stack Initialization */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Creates Default Event Loop Task (Task Priority: 20) */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifiInit = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifiInit));

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); /* Disable Power Save Mode */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); /* Wi-Fi Station + Soft Access Point */

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, wifiConnectHndlr, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifiDisconnectHndlr, NULL));

    /* Configure Wi-Fi Station */
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

    /* Configure Wi-Fi Soft Access Point */
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

    /* Need to set channels now since they CANNOT be set after connecting to Wi-Fi */
    esp_wifi_set_channel(DFLT_CHNL, WIFI_SECOND_CHAN_NONE);

    startWifiRtosConfig();

    xTaskCreatePinnedToCore(&xWifiTask, "WIFI_TASK", STACK_DEPTH, 0, WIFI_PRIO, 0, 0);
}



/* The startWifiRtosConfig() function is used to initialize the
** Semaphores and Mutexes that are used by the xWifiTask and its
** associated functions. The xWifiTask is also created here.
**
** Parameters:
**  none
**
** Return:
**  none
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



/* The wifiConnectHndlr() function is a callback function that
** is entered upon a WIFI_EVENT_STA_CONNECTED event (successful Wi-Fi
** connection). To signify to other tasks that rely on a Wi-Fi connection
** that the Wi-Fi is connected, the xSemWifiStatus is taken.
**
** Parameters:
**  event_handler_arg - non-event based data passed to handler
**  event_base - base type of events
**  event_id - id of event based on base type
**  event_data - data received from the event
**
** Return:
**  none
*/
static void wifiConnectHndlr(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    xSemaphoreTake(xSemWifiStatus, DEF_PEND);

    /* Reset the Wi-Fi connect attempt count */
    setConnectCnt(0);

    ESP_LOGI(TAG, "Connected%s", rtrnNewLine);
}



/* The wifiDisconnectHndlr() function is a callback function that
** is entered upon a WIFI_EVENT_STA_DISCONNECTED event (failed Wi-Fi
** connection). To signify to other tasks that rely on a Wi-Fi connection
** that the Wi-Fi is not connected, the xSemWifiStatus is given. The
** synchronization Semaphore xSemDisconnect is also given to force the
** Wi-Fi task to reattempt connection.
**
** Parameters:
**  event_handler_arg - non-event based data passed to handler
**  event_base - base type of events
**  event_id - id of event based on base type
**  event_data - data received from the event
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

    /* Increment the Wi-Fi connect attempt count until MAX_ATMPT is reached */
    if(localFailCount < MAX_ATMPT)
    {
        setConnectCnt(++localFailCount);
    }

    xSemaphoreGive(xSemDisconnect);
}



/* The wifiCheckStatus() function checks if the xSemWifiStatus 
** Counting Semaphore still has a value or not. If the 
** value is 1, then the Wi-Fi is currently disconnected. If 
** the value is 0, then the Wi-Fi is currently connected.
**
** Parameters:
**  none
**
** Return:
**  An integer of some defined base type with the Semaphore's value
**
** Notes:
*/
UBaseType_t wifiCheckStatus(void)
{
    return uxSemaphoreGetCount(xSemWifiStatus);
}



/* The setConnectCnt() function is the setter function for the
** failedConnectCnt variable, which counts the number of failed 
** connection attempts.
**
** Parameters:
**  newCount - the current connect attempts value
**
** Return:
**  none
*/
static void setConnectCnt(uint8_t newCount)
{
    /* Mutex to Guard failedConnectCnt Variable */
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



/* The getConnectCnt() function is the getter function for the
** failedConnectCnt variable.
**
** Parameters:
**  none
**
** Return:
**  An unsigned integer representing the current connect attempt count
*/
static uint8_t getConnectCnt(void)
{
    uint8_t localFailCount = 0;

    /* Mutex to Guard failedConnectCnt Variable */
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



/* The xWifiTask() function controls the inital connection attempt and
** all subsequent attempts to connect to the Wi-Fi. Connection will only
** be attempted if a WIFI_EVENT_STA_DISCONNECTED event occurs. If the number
** of attempts to connect is below not greater than 11, then the a short delay
** will occur. After this, connection will be re-attempted after a far greater
** (in comparison) period of time.
**
** Parameters:
**  none used
**
** Return:
**  none
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