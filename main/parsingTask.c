/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: parsingTasks.c
** --------
** .......
*/


/* Standard Library Headers */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

/* Driver Headers */
#include "esp_log.h"
#include "esp_err.h"

/* Local Headers */
#include "main.h"
#include "httpTask.h"
#include "parsingTask.h"
#include "uartTasks.h"



/* Local Defines */
#define STATE_SZ 3 // Size for State-related Arrays
#define TAG_SZ 10
#define URL_SZ 30
#define TIME_SZ 3

/* Local Function Declarations */
static void requestParsingTask(void *pvParameters);
static parsingFunc urlToString;
static parsingFunc parseTime;
static parsingFunc parseReserve;
static parsingFunc parseAccessCode;

/* FreeRTOS API Handles */
static SemaphoreHandle_t xSemParseGuard;
static SemaphoreHandle_t xSemHTTPGuard;
static QueueHandle_t xQueueParse;

/* FreeRTOS API Defining Handles */
QueueHandle_t xQueueHttp;

/* Reference Declarations of Global Constant Strings */
extern const char mallocFail[MALLOC_SZ];
extern const char rtrnNewLine[NEWLINE_SZ];
extern const char heapFail[HEAP_SZ];
extern const char mtxFail[MTX_SZ];

/* Local String Constants */
static const char URL[URL_SZ] = "http://172.20.10.3:8000/";
static const char TAG[TAG_SZ] = "REQ_PARSE";

/* Constant Array of Base Branching URL Paths */
static const char *urlPaths[STATE_SZ] =
{
    "time/",
    "reserve/",
    "value/",
};

/* FIXME (Can probably make this a constant and static if it isn't used outside of this context!!!)*/
/* Array of Pointers to Parsing Functions */
static const parsingFuncPtr reqParseFuncs[STATE_SZ] =
{
    parseTime,
    parseReserve,
    parseAccessCode,
};



void startParsingConfig(void)
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


    if(!(xSemParseGuard = xSemaphoreCreateCounting(SEM_CNT, SEM_CNT)))
    {
        ESP_LOGW(TAG, "%s xSemParseGuard%s", heapFail, rtrnNewLine);
    }

    if(!(xQueueParse = xQueueCreate(Q_CNT, sizeof(uint8_t))))
    {
        ESP_LOGW(TAG, "%s xQueueParse%s", heapFail, rtrnNewLine);
    }

    if(!(xSemHTTPGuard = xSemaphoreCreateCounting(SEM_CNT, SEM_CNT)))
    {
        ESP_LOGE(TAG, "%s xSemHTTPGuard%s", heapFail, rtrnNewLine);
    }

    if(!(xQueueHttp = xQueueCreate(Q_CNT, sizeof(requestBodyData*))))
    {
        ESP_LOGE(TAG, "%s xQueueHttp%s", heapFail, rtrnNewLine);
    }

    xTaskCreatePinnedToCore(&requestParsingTask, "REQUEST_PARSE_TASK", \
                            STACK_DEPTH, 0, CORE1_PRIO, 0, 1);
}



bool parseTime(requestBodyData *reqPtr) // FIXME restartTimer()???
{
    bool status = true;

    reqPtr->jsonStrLen = TIME_SZ;
    reqPtr->jsonStr = (char*) malloc(sizeof(char) * (reqPtr->jsonStrLen));

    if((reqPtr->jsonStr) == NULL)
    {
        ESP_LOGE(TAG, "Parsing time %s%s", mallocFail, rtrnNewLine);
        status = false;
    }
    else
    {
        snprintf(reqPtr->jsonStr, reqPtr->jsonStrLen, "{}");
    }

    return status;
}



bool parseReserve(requestBodyData *reqPtr)
{
    bool status = true;

    if(getTimeBool())
    {
        size_t timeStrSize = 0;
        time_t currentTime = time(NULL);
        timeStrSize = snprintf(NULL, 0, "%lld", currentTime);

        reqPtr->jsonStrLen = BASE_RSV_LEN + timeStrSize + 1;
        reqPtr->jsonStr = (char*) malloc(sizeof(char) * (reqPtr->jsonStrLen));

        if(reqPtr->jsonStr == NULL)
        {
            ESP_LOGE(TAG, "Parsing reserve %s%s", mallocFail, rtrnNewLine);
            status = false;
        }
        else
        {
            snprintf(reqPtr->jsonStr, reqPtr->jsonStrLen, \
                    "{\"unixStartTime\": \"%lld\"}", currentTime);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Request made before time set%s", rtrnNewLine);
        timerRestart(reqPtr->id, DEF_FAIL_TOUT);
        status = false;
    }

    return status;
}



bool parseAccessCode(requestBodyData *reqPtr)
{
    bool status = true;

    if(getTimeBool())
    {
        reqPtr->jsonStrLen = BASE_CODE_LEN + ACCESS_LEN + 1;
        reqPtr->jsonStr = (char*) malloc(sizeof(char) * (reqPtr->jsonStrLen));

        if(reqPtr->jsonStr == NULL)
        {
            ESP_LOGE(TAG, "Parsing code %s%s", mallocFail, rtrnNewLine);
            status = false;
        }
        else
        {
            snprintf(reqPtr->jsonStr, reqPtr->jsonStrLen, "{\"accessCode\": \"%ld\"}", \
                    getAccessCode());

            ESP_LOGI(TAG, "String: %s", reqPtr->jsonStr);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Request made before time set%s", rtrnNewLine);
        status = false;
    }

    return status;
}



bool urlToString(requestBodyData *reqPtr)
{
    bool status = true;

    reqPtr->urlLen = (snprintf(NULL, 0, "%s%s", URL, urlPaths[reqPtr->id])) + 1;
    reqPtr->url = (char*) malloc(sizeof(char) * (reqPtr->urlLen));

    if(reqPtr->url == NULL)
    {
        ESP_LOGE(TAG, "URL %s%s", mallocFail, rtrnNewLine);
        status = false;
    }
    else
    {
        snprintf(reqPtr->url, reqPtr->urlLen, "%s%s", URL, urlPaths[reqPtr->id]);
    }

    return status;
}



void queuingParseData(uint8_t idVal)
{
    if(xSemaphoreTake(xSemParseGuard, DEF_PEND))
    {
        if(!xQueueSendToBack(xQueueParse, (void*) &idVal, DEF_PEND))
        {
            ESP_LOGE(TAG, "Could not send to xQueueHTTP%s", rtrnNewLine);
        }   
    }
    else
    {
        ESP_LOGW(TAG, "xQueueHTTP is full%s", rtrnNewLine);
    }
}



static int8_t queuingHttpData(requestBodyData *reqPtr, int8_t freeHeapCntr)
{
    if(xSemaphoreTake(xSemHTTPGuard, DEF_PEND))
    {
        if(!xQueueSendToBack(xQueueHttp, (void*) &reqPtr, DEF_PEND))
        {
            ESP_LOGE(TAG, "Could not send to xQueueHTTP%s", rtrnNewLine);
            freeHeapCntr = MAX_HEAP;
        }   
    }
    else
    {
        ESP_LOGW(TAG, "xQueueHTTP is full%s", rtrnNewLine);
        freeHeapCntr = MAX_HEAP;
    }

    return freeHeapCntr;
}



void giveSemHttpGuard(void)
{
    xSemaphoreGive(xSemHTTPGuard);
}



void mallocCleanup(requestBodyData *reqPtr, int8_t mallocCnt)
{
    switch(mallocCnt)
    {
        case MAX_HEAP:
            free(reqPtr->url);
            /* Fall-through */
        case HIGH_HEAP:
            free(reqPtr->jsonStr);
            /* Fall-through */
        case LOW_HEAP:
            //timerRestart(reqPtr->id);
            free(reqPtr);
            break;
        default:
            break;
    } /* End Switch Statement */
}



static void requestParsingTask(void *pvParameters)
{
    while(xQueueParse == NULL) 
    {
        vTaskDelay(QUICK_DELAY);
    }

    while(true)
    {
        int8_t freeHeapCntr = NO_HEAP;
        uint8_t idVal = 0;

        xQueueReceive(xQueueParse, &idVal, portMAX_DELAY);
        xSemaphoreGive(xSemParseGuard);

        requestBodyData *dataPtr = (requestBodyData*) malloc(sizeof(requestBodyData));

        if(dataPtr == NULL)
        {
            ESP_LOGE(TAG, "Request body data %s%s", mallocFail, rtrnNewLine);
            timerRestart(idVal, DEF_FAIL_TOUT);
            continue;
        }
        dataPtr->id = idVal;
        
        if(reqParseFuncs[dataPtr->id](dataPtr))
        {
            if(urlToString(dataPtr))
            {
               freeHeapCntr = queuingHttpData(dataPtr, freeHeapCntr); 
            }
            else
            {
                freeHeapCntr = HIGH_HEAP;
            }
        }
        else
        {
            freeHeapCntr = LOW_HEAP;
        }

        mallocCleanup(dataPtr, freeHeapCntr);

    }
}