/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: httpTask.c
** --------
** Enables HTTP Request capabilites for the ESP device
** and configures several oneshot timers related to these
** HTTP Requests.
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



/* Variable Naming Abbreviations Legend:
**
** Sem - Semaphore
** Mtx - Mutex
** Rtrn - Return
** Len - Length
** Rsv - Reserve
** Resp - Response
** Def - Default
** Tout - Timeout
** Sz - Size
** Tx - Transmit
** Rx - Receive
** Mem - Memory
**
*/



/* Local Defines */
#define TIMER_SZ 2 /* Size of Timer Args Array */
#define HTTP_TOUT 250               /* Time in milliseconds*/
#define RSV_DEF_TOUT 60000000       /* Time in microseconds*/
#define TIME_DEF_TOUT 86400000000   //  |                  
#define RSV_FAIL_TOUT 20000000      //  |
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



/* The startHttpConfig() the two oneshot timers that call
** for HTTP requests upon expiring. One is for making reservation
** info POST requests and the other is for making server time POST
** requests.
**
** Parameters:
**  none
**
** Return:
**  none
*/
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



/* The startHttpRtosConfig() function is used to initialize the
** Semaphore and Queue that are used by the xHttpTask and its
** associated functions. The xHttpTask is also created here.
**
** Parameters:
**  none
**
** Return:
**  none
*/
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



/* The oneshotCallback() function is a re-entrant function used by both
** high resolution timers upon their expiration. It then checks the Wi-Fi status
** Semaphore. If the Wi-Fi is connected, then the timer (request) ID will be 
** passed to the queuingParseData function. Regardless of connection status,
** the timer will restart. However, the timer's time until expiration will vary
** based on the connection's status.
**
** Parameters:
**  args - passes a pointer to typedef struct timerArgsStruct
**
** Return:
**  none
**
** Notes: The priority of the High Resolution Timers task is 22 in ESP-IDF.
** The callback makes use of a pointer to structs that contain their timer handle information
** in addition to their ID. The timer IDs correlate to the POST request and
** response IDs, so ID 0 is for server time requests and ID 1 is for reservation
** info requests. Conditional expressions were used to give timers their specific timeout period
** while making the function re-entrant
*/
static void oneshotCallback(void *args)
{
    uint64_t delayTime;
    timerArgsStruct *timerArgsPtr = (timerArgsStruct*) args;

    if (!wifiCheckStatus())
    {
        delayTime = (timerArgsPtr->timerNum == 0) ? TIME_DEF_TOUT : RSV_DEF_TOUT;
        queuingParseData(timerArgsPtr->timerNum); /* timerNum == ID */
    }
    else
    {
        delayTime = (timerArgsPtr->timerNum == 0) ? TIME_FAIL_TOUT : RSV_FAIL_TOUT;
    }
    esp_timer_start_once(*(timerArgsPtr->timerHndl), delayTime); /* This API must be used AFTER timer reaches zero */
}



/* The queuingUartTxData() function is used to queue the data received from the POST
** response. Like all other queuing functions, this one uses a Counting Semaphore to
** guard against queue overflow in addition to making sure that the value was added
** to the queue. 
**
** Parameters:
**  ptr - pointer to a cJSON object
**  memSafe - boolean variable that states whether or not queue allocation has failed
**
** Return:
**  Boolean specifying if the queue failed and if the cJSON object needs to be "freed"
**
** Notes: The cJSON library is used to format the JSON string received in the 
** response into a struct like object whose values can be accessed. The reason
** memSafe is used here is because if this fails to queue, then we need to free
** up the memory allocated by creating the cJSON object!
*/
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



/* The postRespHndlr() function is called upon any HTTP event, but only
** executes its functionality when the Event ID is 'HTTP_EVENT_ON_DATA'.
** Once the data is received from the response, it is handled here by 
** making it into a cJSON object (info on cJSOn purpose in queuingUartTxData()
** function notes). After checking that the responseID and responseCode exist,
** we pass it to a switch statement that handles the different response codes
** (of which only three are viable).
**
** Parameters:
**  event - the struct for an HTTP event
**
** Return:
** A typedef integer that represents a status code for ESP-IDF
**
** Notes: For cJSON objects, you only need to free the original object
** (in this case responsePtr) you created and not the 'sub-objects' that
** branch from it (like responseId). Further details on the meaning of
** certain response codes can be found in the Django Python code (specifically
** the views.py file). Remember, the timer IDs correspond to the POST response
** IDs, so if the POST fails, we can restart the timers here.
*/
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
                /* Don't want to wait full default timeout if we failed here! */
                timerRestart(responseId->valueint, DEF_FAIL_TOUT); /* Access Code Exception Handled in timerRestart()! */
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



/* The timerRestart() function provides a means to restart timers throughout
** overall code space upon some failure (like in the postRespHndlr() function for
** instance). Since the IDs of the timers, requests, and responses correlate to one
** another, all that is needed is the timer ID and timeout period for this function.
**
** Parameters:
**  timerNum - the id or rather number associated with the timer
**  timeout - the duration of time which the timer should count down from
**
** Return:
**  none
**
** Notes: You might have noticed that due to the way the postRespHndlr() function
** is written, it may be possible to send a timer ID that does not exist to the
** timerRestart() function. However, this case is handled here through the switch
** statements default case!
*/
void timerRestart(uint8_t timerNum, uint64_t timeout)
{
    switch(timerNum)
    {
        case TIME_ID:
            /* Fall-through */
        case RSV_ID: 
            esp_timer_restart(*(timerArgsArr[timerNum].timerHndl), timeout);
            break;
        
        default:
            ESP_LOGW(TAG, "Attempted restart of non-existent timer%s", rtrnNewLine);
            break;
    } /* End Switch Statement */
}



/* The giveSemUartTxGuard() function is an abstraction function that
** gives the xSemUartTxGuard Counting Semaphore. This Counting Semaphore is
** used in conjunction with the xQueueUartTx Queue in order to guard against overflow.
**
** Parameters:
**  none
**
** Return:
**  none
*/
void giveSemUartTxGuard(void)
{
    xSemaphoreGive(xSemUartTxGuard);
}



/* The xHttpTask() function handles the creation, transmission, and deletion
** of HTTP Requests. (It also starts the two high resolution timers for their
** initial time periods which are equivalent to their failure periods.) The
** function pends indefinitely on the xQueueHttp Queue until receiving a pointer to
** a typedef struct requestBodyData. It uses the information stored in this struct
** to fill out the HTTP config.
**
** Parameters:
**  none used
**
** Return:
**  none
**
** Notes: Redundancy is used by retransmitting HTTP Request 11 times (if they fail) 
** before stopping Request attempts. The timerRestart() function also makes an appearance here.
*/
static void xHttpTask(void *pvParameters)
{
    esp_timer_start_once(timeRequest, TIME_FAIL_TOUT);
    esp_timer_start_once(reserveRequest, RSV_FAIL_TOUT);

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
        
        /* Used to cleanup the requestBodyData Struct */
        mallocCleanup(dataPtr, MAX_HEAP);
    }
}