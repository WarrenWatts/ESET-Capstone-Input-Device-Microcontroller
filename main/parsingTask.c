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



/* Variable Naming Abbreviations Legend:
**
** Sem - Semaphore
** Mtx - Mutex
** Rtrn - Return
** Len - Length
** Rsv - Reserve
** Def - Default
** Tout - Timeout
** Sz - Size
** Req - Request
** Cntr - Counter
**
*/



/* Local Defines */
#define TIME_LEN (POST_STATE_SZ)

/* Enum for Local Constant String Sizes */
typedef enum
{
    PARSE_RSV_LEN = 15,
    PARSE_CODE_LEN = 18,
    DEF_RSV_LEN = 21,
    DEF_CODE_LEN = 25,
    URL_LEN,
    EARLY_FAIL_LEN = 32,
} localStrLengths;

/* Local Function Declarations */
static void xParsingTask(void *pvParameters);
static parsingFunc urlToString;
static parsingFunc parseTime;
static parsingFunc parseReserve;
static parsingFunc parseAccessCode;

/* FreeRTOS Local API Handles */
static SemaphoreHandle_t xSemParseGuard;
static SemaphoreHandle_t xSemHTTPGuard;
static QueueHandle_t xQueueParse;

/* FreeRTOS Defining API Handles */
QueueHandle_t xQueueHttp;

/* Reference Declarations of Global Constant Strings */
extern const char mallocFail[MALLOC_LEN];
extern const char rtrnNewLine[NEWLINE_LEN];
extern const char heapFail[HEAP_LEN];
extern const char mtxFail[MTX_LEN];

/* Defining Declarations of Global Constant Strings */
const char queueSendFail[SEND_FAIL_LEN] = "Could not send to ";
const char queueFullFail[FULL_FAIL_LEN] = " is full";

/* Local String Constants */
static const char URL[URL_LEN] = "http://172.20.10.3:8000/";
static const char TAG[TAG_LEN_10] = "REQ_PARSE";
static const char earlyBirdFail[EARLY_FAIL_LEN] = "Request made before time set in";

/* Constant Array of Pointers of Base Branching URL Paths */
static const char *urlPaths[POST_STATE_SZ] =
{
    "time/",
    "reserve/",
    "value/",
};

/* Constant State Array of Pointers to Functions */
static const parsingFuncPtr reqParseFuncs[POST_STATE_SZ] =
{
    parseTime,
    parseReserve,
    parseAccessCode,
};



/* The startParsingConfig() function is used to initialize the
** Semaphores and Queues used by the xParsingTask() and its
** associated functions. The xParsingTask() is also created here.
**
** Parameters:
**  none
**
** Return:
**  none
*/
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
        ESP_LOGE(TAG, "%s xSemParseGuard%s", heapFail, rtrnNewLine);
    }

    if(!(xQueueParse = xQueueCreate(Q_CNT, sizeof(uint8_t))))
    {
        ESP_LOGE(TAG, "%s xQueueParse%s", heapFail, rtrnNewLine);
    }

    if(!(xSemHTTPGuard = xSemaphoreCreateCounting(SEM_CNT, SEM_CNT)))
    {
        ESP_LOGE(TAG, "%s xSemHTTPGuard%s", heapFail, rtrnNewLine);
    }

    if(!(xQueueHttp = xQueueCreate(Q_CNT, sizeof(requestBodyData*))))
    {
        ESP_LOGE(TAG, "%s xQueueHttp%s", heapFail, rtrnNewLine);
    }

    xTaskCreatePinnedToCore(&xParsingTask, "REQUEST_PARSE_TASK", \
                            STACK_DEPTH, 0, CORE1_PRIO, 0, 1);
}



bool parseTime(requestBodyData *reqPtr)
{
    bool status = true;

    reqPtr->jsonStrLen = TIME_LEN;
    reqPtr->jsonStr = (char*) malloc(sizeof(char) * (reqPtr->jsonStrLen));

    if((reqPtr->jsonStr) == NULL)
    {
        ESP_LOGE(TAG, "parseTime() %s%s", mallocFail, rtrnNewLine);
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
    static const char funcNameStr[PARSE_RSV_LEN] = "parseReserve()";
    bool status = true;

    if(getTimeBool())
    {
        size_t timeStrLen = 0;
        time_t currentTime = time(NULL);
        timeStrLen = snprintf(NULL, 0, "%lld", currentTime);

        reqPtr->jsonStrLen = DEF_RSV_LEN + timeStrLen + 1;
        reqPtr->jsonStr = (char*) malloc(sizeof(char) * (reqPtr->jsonStrLen));

        if(reqPtr->jsonStr == NULL)
        {
            ESP_LOGE(TAG, "%s %s%s", funcNameStr, mallocFail, rtrnNewLine);
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
        ESP_LOGE(TAG, "%s %s%s", earlyBirdFail, funcNameStr, rtrnNewLine);
        timerRestart(reqPtr->id, DEF_FAIL_TOUT);
        status = false;
    }

    return status;
}



bool parseAccessCode(requestBodyData *reqPtr)
{ /* FIXME : Now that this function is no longer unique, combine/refactor with parseReserve()! */
    static const char funcNameStr[PARSE_CODE_LEN] = "parseAccessCode()";
    bool status = true;

    if(getTimeBool())
    {
        reqPtr->jsonStrLen = DEF_CODE_LEN + 1;
        reqPtr->jsonStr = (char*) malloc(sizeof(char) * (reqPtr->jsonStrLen));

        if(reqPtr->jsonStr == NULL)
        {
            ESP_LOGE(TAG, "%s %s%s", funcNameStr, mallocFail, rtrnNewLine);
            status = false;
        }
        else
        {
            snprintf(reqPtr->jsonStr, reqPtr->jsonStrLen, "{\"accessCode\": \"%ld\"}", \
                    getAccessCode());
        }
    }
    else
    {
        ESP_LOGE(TAG, "%s %s%s", earlyBirdFail, funcNameStr, rtrnNewLine);
        status = false;
    }

    return status;
}



void queuingParseData(uint8_t idVal)
{
    if(xSemaphoreTake(xSemParseGuard, DEF_PEND))
    {
        if(!xQueueSendToBack(xQueueParse, (void*) &idVal, DEF_PEND))
        {
            ESP_LOGE(TAG, "%sxQueueHTTP%s", queueSendFail, rtrnNewLine);
        }   
    }
    else
    {
        ESP_LOGE(TAG, "%sxQueueHTTP%s", queueFullFail, rtrnNewLine);
    }
}



static int8_t queuingHttpData(requestBodyData *reqPtr, int8_t freeHeapCntr)
{
    if(xSemaphoreTake(xSemHTTPGuard, DEF_PEND))
    {
        if(!xQueueSendToBack(xQueueHttp, (void*) &reqPtr, DEF_PEND))
        {
            ESP_LOGE(TAG, "%sxQueueHTTP%s", queueSendFail, rtrnNewLine);
            freeHeapCntr = MAX_HEAP;
        }   
    }
    else
    {
        ESP_LOGE(TAG, "%sxQueueHTTP%s", queueFullFail, rtrnNewLine);
        freeHeapCntr = MAX_HEAP;
    }

    return freeHeapCntr;
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
            free(reqPtr);
            break;
        default:
            break;
    } /* End Switch Statement */
}



/* The giveSemHttpGuard() function is an abstraction function that
** gives the xSemHttpGuard Counting Semaphore. This Counting Semaphore is
** used in conjunction with the xQueueHttp Queue in order to guard against overflow.
**
** Parameters:
**  none
**
** Return:
**  none
*/
void giveSemHttpGuard(void)
{
    xSemaphoreGive(xSemHTTPGuard);
}



static void xParsingTask(void *pvParameters)
{
    while(xQueueParse == NULL) 
    {
        vTaskDelay(SEC_DELAY);
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
            ESP_LOGE(TAG, "xParsingTask() %s%s", mallocFail, rtrnNewLine);
            timerRestart(idVal, DEF_FAIL_TOUT);
            continue;
        }
        dataPtr->id = idVal;
        bool heapFail = false;

        if(reqParseFuncs[idVal](dataPtr))
        {
            if(urlToString(dataPtr))
            {
               freeHeapCntr = queuingHttpData(dataPtr, freeHeapCntr); 
            }
            else
            {
                freeHeapCntr = HIGH_HEAP;
                heapFail = true;
            }
        }
        else
        {
            freeHeapCntr = LOW_HEAP;
            heapFail = true;
        }
        
        if(heapFail)
        {
            timerRestart(idVal, DEF_FAIL_TOUT);
        }

        mallocCleanup(dataPtr, freeHeapCntr);
    }
}