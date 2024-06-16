/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: parsingTasks.c
** --------
** Code to handle the preparation of data
** that will be used in each specific HTTP Reqeust.
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
    URL_LEN = 31,
    EARLY_FAIL_LEN = 32,
} localStrLengths;

/* Local Function Declarations */
static void xParsingTask(void *pvParameters);
static int8_t queuingHttpData(requestBodyData *reqPtr, int8_t freeHeapCntr);
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

/* Defining Declarations of Global Constant Strings */
const char queueSendFail[SEND_FAIL_LEN] = "Could not send to ";
const char queueFullFail[FULL_FAIL_LEN] = " is full";

/* Local String Constants */
static const char URL[URL_LEN] = "http://255.255.255.255:8000/"; /* Will be changing... */
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



/* BIG NOTE:
** Due to how ESP-IDF API works with processing "JSON" in the form
** of strings (won't accept an empty string between the brackets)
** as well as the original plan for parsing the access code
** (which involved two name/value pairs instead of one), parsing
** was done in three separate functions. To whomever picks up this
** Capstone project next, it should be possible to re-work the code
** into having a singular function that handles all parsing, especially
** since now the functions parseReserve() and parseAccessCode() are so
** similar and therefore redundant.
*/



/* The parseTime() function creates the necessary JSON string for the body
** of the HTTP POST Request that will return the server time. In this case,
** the body need only be brackets.
**
** Parameters:
**  reqPtr - pointer to the requestBodyData struct in dynamic memory
**
** Return:
**  A Boolean on the status of memory allocation success
**
** Notes: An empty string or rather "" did not seem to work properly
** due to the way ESP-IDF processes their JSON strings. May want to look
** further into this!
*/
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



/* The parseReserve() function creates the necessary JSON string for the body
** of the HTTP POST Request that will return the reservation information.
**
** Parameters:
**  reqPtr - pointer to the requestBodyData struct in dynamic memory
**
** Return:
**  A Boolean on the status of memory allocation success
*/
bool parseReserve(requestBodyData *reqPtr)
{
    static const char funcNameStr[PARSE_RSV_LEN] = "parseReserve()";
    bool status = true;

    if(getTimeBool())
    {
        size_t timeStrLen = 0;
        time_t currentTime = getTime();

        /* In case a server time larger than what is expected appears */
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



/* The parseAccessCode() function creates the necessary JSON string for the body
** of the HTTP POST Request that will return the access code validation info.
**
** Parameters:
**  reqPtr - pointer to the requestBodyData struct in dynamic memory
**
** Return:
**  A Boolean on the status of memory allocation success
*/
bool parseAccessCode(requestBodyData *reqPtr)
{ /* FIXME : Now that this function is no longer unique, try to combine it with parseReserve() and parseTime! */
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



/* The queuingParseData() function is used to queue the ID value parsed used to determine
** the type of POST Request data to begin preparing. Like all other queuing functions, 
** this one uses a Counting Semaphore to guard against queue overflow in addition to 
** making sure that the value was added to the queue. 
**
** Parameters:
**  idVal - the ID of the POST Request to be sent
**
** Return:
**  none
*/
void queuingParseData(uint8_t idVal)
{
    if(xSemaphoreTake(xSemParseGuard, DEF_PEND))
    {
        if(!xQueueSendToBack(xQueueParse, (void*) &idVal, DEF_PEND))
        {
            ESP_LOGE(TAG, "%sxQueueHTTP%s", queueSendFail, rtrnNewLine);
            xSemaphoreGive(xSemParseGuard);
        }   
    }
    else
    {
        ESP_LOGE(TAG, "%sxQueueHTTP%s", queueFullFail, rtrnNewLine);
    }
}



/* The queuingHttpData() function is used to queue the data parsed for the HTTP POST
** request. Like all other queuing functions, this one uses a Counting Semaphore to
** guard against queue overflow in addition to making sure that the value was added
** to the queue. 
**
** Parameters:
**  reqPtr - pointer to the requestBodyData struct in dynamic memory
**  freeHeapCntr - integer that determines how much dynamic memory has to be freed
**
** Return:
**  The amount of dynamic memory that needs to be freed
*/
static int8_t queuingHttpData(requestBodyData *reqPtr, int8_t freeHeapCntr)
{
    if(xSemaphoreTake(xSemHTTPGuard, DEF_PEND))
    {
        if(!xQueueSendToBack(xQueueHttp, (void*) &reqPtr, DEF_PEND))
        {
            ESP_LOGE(TAG, "%sxQueueHTTP%s", queueSendFail, rtrnNewLine);
            giveSemHttpGuard();
            freeHeapCntr = MAX_HEAP;
        }
        /* Don't want memory freed if there is no failure! */   
    }
    else
    {
        ESP_LOGE(TAG, "%sxQueueHTTP%s", queueFullFail, rtrnNewLine);
        freeHeapCntr = MAX_HEAP;
    }

    return freeHeapCntr;
}



/* The urlToString() function allocates and creates the string
** that will be used as the URL for the HTTP POST Request.
**
** Parameters:
**  reqPtr - pointer to the requestBodyData struct in dynamic memory
**
** Return:
**  A Boolean on the status of memory allocation success
*/
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



/* The mallocCleanup() function is used to cleanup up each of the dynamically
** allocated piece within the typedef struct requestBodyData and the struct 
** itself. The value of the heap (i.e., how much has been allocated
** before this function was called and now needs cleaning) tells you how 
** much needs to be cleaned up, and a fall-through switch statement helped
** to make the code for this process much cleaner.
**
** Parameters:
**  reqPtr - pointer to the requestBodyData struct in dynamic memory
** mallocCnt - the number of mallocs that DON'T need freeing
**
** Return:
**  none
*/
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



/* The xParsingTask() function is used to handle the preparation of data
** that will be used in each HTTP Reqeust. A state array of pointers to functions is used
** to gather the correct data for each HTTP Request based on their ID, the ID value
** passed via the Queue being used as the index which chooses the correct function. A
** typedef struct requestBodyData is dynamically allocated and passed to these functions
** to set and carry strings and their lengths needed for the Request. Once completed,
** it is then passed on to the xQueueHTTP Queue.
** 
**
** Parameters:
**  none used
**
** Return:
**  none
**
** Notes: Since these functions will generate further dynamic memory, if else statements
** and a "counter-like" variable are used to determine the amount of heap that 
** needs to be freed in the event of a failure.
*/
static void xParsingTask(void *pvParameters)
{
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