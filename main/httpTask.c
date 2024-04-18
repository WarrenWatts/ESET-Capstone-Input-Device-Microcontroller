/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: xHttpTask.c
** --------
** .......
*/

/* Standard Library Headers */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

/* Driver Headers */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"

/* Local Headers */
#include "wifiTask.h"
#include "main.h"
#include "parsingTask.h"
#include "httpTask.h"

/* Other Headers */
#include "cJSON.h"



/* Local Defines */
#define TIMER_SZ 2 /* Size of Timer Args Array */
#define HTTP_TOUT 250               /* Time in milliseconds*/
#define RES_DEF_TOUT 60000000       /* Time in microseconds*/
#define TIME_DEF_TOUT 86400000000   //  |                  
#define RES_FAIL_TOUT 20000000      //  |
#define TIME_FAIL_TOUT 15000000     //  V

/* Static Function Declarations */
static void oneshotCallback(void *args);
static void xHttpTask(void *pvParameters);
static void startRtosHttpConfig(void);
static bool queuingUartTxData(cJSON *ptr, bool memSafe);
static esp_err_t postRespHndlr(esp_http_client_event_handle_t event);

/* FreeRTOS Local API Handles */
static SemaphoreHandle_t xSemUartTxGuard;

/* FreeRTOS Reference API Handles */
extern QueueHandle_t xQueueHttp;

/* FreeRTOS Defining API Handles */
QueueHandle_t xQueueUartTx;

/* ESP Timer Handles */
static esp_timer_handle_t reserveRequest;
static esp_timer_handle_t timeRequest;

/* Reference Declarations of Global Constant Strings */
extern const char rtrnNewLine[NEWLINE_LEN];
extern const char heapFail[HEAP_LEN];
extern const char mtxFail[MTX_LEN];
extern const char queueSendFail[SEND_FAIL_LEN];
extern const char queueFullFail[FULL_FAIL_LEN];

/* Local Constant Logging String */
static const char TAG[TAG_LEN_9] = "ESP_HTTP";

/* Constant Array of timerArgsStructs */
static const timerArgsStruct timerArgsArr[TIMER_SZ] =
{
    {TIME_ID, &timeRequest},
    {RSV_ID, &reserveRequest},
};



void startHttpConfig(void)
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

    esp_timer_create_args_t oneshotTimeArgs = 
    {
        .callback = oneshotCallback,
        .arg = (void*) &timerArgsArr[TIME_ID],
        .name = "oneshot",
    };
    ESP_ERROR_CHECK(esp_timer_create(&oneshotTimeArgs, &timeRequest));

    esp_timer_create_args_t oneshotReserveArgs = 
    {
        .callback = oneshotCallback,
        .arg = (void*) &timerArgsArr[RSV_ID],
        .name = "oneshot",
    };
    ESP_ERROR_CHECK(esp_timer_create(&oneshotReserveArgs, &reserveRequest));

    startRtosHttpConfig();
}



static void startRtosHttpConfig(void)
{
    if(!(xQueueUartTx = xQueueCreate(Q_CNT, sizeof(requestBodyData*))))
    {
        ESP_LOGW(TAG, "%s xQueueUartTx %s", heapFail, rtrnNewLine);
    }

    if(!(xSemUartTxGuard = xSemaphoreCreateCounting(SEM_CNT, SEM_CNT)))
    {
        ESP_LOGW(TAG, "%s xSemUartTxGuard %s", heapFail, rtrnNewLine);
    }

    xTaskCreatePinnedToCore(&xHttpTask, "HTTP_TASK", STACK_DEPTH, 0, HTTP_PRIO, 0, 0);
}



void giveSemUartTxGuard(void)
{
    xSemaphoreGive(xSemUartTxGuard);
}



static bool queuingUartTxData(cJSON *ptr, bool memSafe)
{
    if(xSemaphoreTake(xSemUartTxGuard, DEF_PEND))
    {
        if(!xQueueSendToBack(xQueueUartTx, (void*) &ptr, DEF_PEND))
        {
            ESP_LOGE(TAG, "%sxQueueUartTx%s", queueSendFail, rtrnNewLine);
            memSafe = false;
        }   
    }
    else
    {
        ESP_LOGW(TAG, "xQueueUartTx%s%s", queueFullFail, rtrnNewLine);
        memSafe = false;
    }

    return memSafe;
}



static esp_err_t postRespHndlr(esp_http_client_event_handle_t event)
{
    bool memSafe = true;
    if((event->event_id) == HTTP_EVENT_ON_DATA)
    {
        cJSON *responsePtr = cJSON_ParseWithLength(event->data, \
                                                        event->data_len);

        cJSON *responseId = cJSON_GetObjectItemCaseSensitive(responsePtr, "id");

        cJSON *responseCode = cJSON_GetObjectItemCaseSensitive(responsePtr, \
                                                                    "responseCode");
        
        if(!(cJSON_IsNumber(responseCode)) ||
            !(cJSON_IsNumber(responseId)))
        {
            ESP_LOGE(TAG, "JSON Fail in Response Handler%s", rtrnNewLine);
            cJSON_Delete(responsePtr);
            return ESP_OK;
        }
        
        switch(responseCode->valueint)
        {
            case VALID_RESP:
                /* Fall-through */
            case INVALID_RESP:
                /* Fall-through */
            case NO_RSV_RESP:
                memSafe = queuingUartTxData(responsePtr, memSafe);
                break;
            
            default:
                timerRestart(responseId->valueint, DEF_FAIL_TOUT); /* Access Code Exception Handled in function! */
                ESP_LOGE(TAG, "Invalid - Response Code: %d", responseCode->valueint);
                break;
        } /* End Switch Statement */

        if(!memSafe)
        {
            cJSON_Delete(responsePtr);
        }
    }

    return ESP_OK;
}



static void oneshotCallback(void *args)
{
    uint64_t delayTime;
    timerArgsStruct *timerArgsPtr = (timerArgsStruct*) args;

    if (!wifiCheckStatus())
    {
        delayTime = (timerArgsPtr->timerNum == 0) ? TIME_DEF_TOUT : RES_DEF_TOUT;
        queuingParseData(timerArgsPtr->timerNum);
    }
    else
    {
        delayTime = (timerArgsPtr->timerNum == 0) ? TIME_FAIL_TOUT : RES_FAIL_TOUT;
    }
    esp_timer_start_once(*(timerArgsPtr->timerHndl), delayTime);
}



void timerRestart(uint8_t timerNum, uint64_t timeout)
{
    switch(timerNum)
    {
        case 0:
            /* Fall-through */
        case 1: 
            esp_timer_restart(*(timerArgsArr[timerNum].timerHndl), timeout);
            break;
        
        default:
            ESP_LOGW(TAG, "Attempted restart of non-existent timer%s", rtrnNewLine);
            break;
    } /* End Switch Statement */
}



static void xHttpTask(void *pvParameters)
{
    esp_timer_start_once(timeRequest, TIME_FAIL_TOUT);
    esp_timer_start_once(reserveRequest, RES_FAIL_TOUT);

    while(true)
    {
        requestBodyData *dataPtr = 0;
        uint8_t count;
        
        xQueueReceive(xQueueHttp, &dataPtr, portMAX_DELAY);
        giveSemHttpGuard();
        
        esp_http_client_config_t postReqConfig = 
        {
            .url = dataPtr->url,
            .method = HTTP_METHOD_POST,
            .cert_pem = NULL,
            .event_handler = postRespHndlr,
            .timeout_ms = HTTP_TOUT,
        };

        esp_http_client_handle_t client = esp_http_client_init(&postReqConfig);
        esp_http_client_set_post_field(client, dataPtr->jsonStr, (dataPtr->jsonStrLen) - 1);
        esp_http_client_set_header(client, "Content-Type", "application/json");

        for(count = 0; count < MAX_ATMPT; count++)
        {
            if(esp_http_client_perform(client) == ESP_OK)
            {
                break;
            }
        }
        esp_http_client_cleanup(client);

        if(count == MAX_ATMPT)
        {
            timerRestart(dataPtr->id, DEF_FAIL_TOUT);
        }
        
        mallocCleanup(dataPtr, MAX_HEAP);
    }
}