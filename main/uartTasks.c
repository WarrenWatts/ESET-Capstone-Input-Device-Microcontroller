/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: uartTasks.c
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
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

/* Local Headers */
#include "wifiTask.h"
#include "espnowTask.h"
#include "httpTask.h"
#include "parsingTask.h"
#include "main.h"
#include "uartTasks.h"
#include "ledTask.h"

/* Other Headers */
#include "cJSON.h"



/* Local Defines */
#define NO_RSV_LEN 12 // FIXME 
#define TAG_SZ 8
#define TX_BUF_SZ 4
#define MICRO_SEC_FACTOR 1000000

/* Static Function Declarations */
static void xUartRxTask(void *pvParameters);
static void xUartTxTask(void *pvParameters);
static void setAccessCode(char *buffer);
static void setTimeBool(bool localTimeSetBool);
static void uartRxWorkHndlr(void);
static char* reserveTimesHndlr(cJSON *reserveTime);
static bool espnowMtxHndlr(void);
static void startUartRtosConfig(void);
static printingFunc printTime;
static printingFunc printValid;
static printingFunc printReserve;

/* */
static SemaphoreHandle_t xMtxTimeBool;
static SemaphoreHandle_t xMtxAccessCode;
static SemaphoreHandle_t xMtxParanoid;
static QueueHandle_t xQueueUartRx;

/* */
extern QueueHandle_t xQueueUartTx;
extern SemaphoreHandle_t xMtxEspnow;
extern SemaphoreHandle_t xSemEspnow;

/* */
extern const char mallocFail[MALLOC_SZ];
extern const char heapFail[HEAP_SZ];
extern const char mtxFail[MTX_SZ];
extern const char rtrnNewLine[NEWLINE_SZ];

/* */
static const char noRsv[NO_RSV_LEN] = "Reservation"; // POSSIBLY REMOVE ME
static const char TAG1[TAG_SZ] = "UART_RX";
static const char TAG2[TAG_SZ] = "UART_TX";

/* */
static bool timeSetBool = false;

static int32_t accessCode = 0;

/* */
printingFuncPtr respPrintFuncs[3] = // FIXME (MAGIC NUMBER!!!)
{
    printTime,      // ID: 0
    printReserve,   // ID: 1
    printValid      // ID: 2
};



// FIX ME (ADD Initialized Check)
void startUartConfig(void)
{
    uart_config_t uartConfig = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intrAllocFlags = 0;

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE, 0, TX_BUF_SZ, &xQueueUartRx, intrAllocFlags));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uartConfig));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, RTS_PIN, CTS_PIN));

    startUartRtosConfig();
}



static void startUartRtosConfig(void)
{
    if(!(xMtxTimeBool = xSemaphoreCreateMutex()))
    {
        ESP_LOGE(TAG1, "%s xMtxTimeBool%s", heapFail, rtrnNewLine);
    }

    if(!(xMtxAccessCode = xSemaphoreCreateMutex()))
    {
        ESP_LOGE(TAG1, "%s xMtxAccessCode%s", heapFail, rtrnNewLine);
    }

    if(!(xMtxParanoid = xSemaphoreCreateCounting(PSEUDO_MTX_CNT, PSEUDO_MTX_CNT)))
    {
        ESP_LOGE(TAG1, "%s xMtxParanoid%s", heapFail, rtrnNewLine);
    }

    xTaskCreatePinnedToCore(&xUartRxTask, "RX_TASK", STACK_DEPTH, 0, CORE1_PRIO, 0, 1);
    xTaskCreatePinnedToCore(&xUartTxTask, "TX_TASK", STACK_DEPTH, 0, CORE1_PRIO, 0, 1);
}



bool getTimeBool(void)
{
    bool localTimeSetBool = NULL; // FIXME (MAY NOT DO WHAT I THINK IT DOES) DEFINITELY FIXME
    if(xSemaphoreTake(xMtxTimeBool, DEF_PEND))
    {
        localTimeSetBool = timeSetBool;
        xSemaphoreGive(xMtxTimeBool);
    }
    else
    {
        ESP_LOGE(TAG2, "xMtxTimeBool get() %s%s", mtxFail, rtrnNewLine);
    }

    return localTimeSetBool;
}



static void setTimeBool(bool localTimeSetBool)
{
    if(xSemaphoreTake(xMtxTimeBool, DEF_PEND))
    {
        timeSetBool = localTimeSetBool;
        xSemaphoreGive(xMtxTimeBool);
    }
    else
    {
        ESP_LOGE(TAG2, "xMtxTimeBool set() %s%s", mtxFail, rtrnNewLine);
    }
}



static struct tm* setTime(int serverTime)
{
    time_t currentTime;
    struct tm *timePtr = NULL;
    struct timeval tv = {(time_t) serverTime, 0};

    settimeofday(&tv, 0);
    setTimeBool(true);
    time(&currentTime);
    timePtr = localtime(&currentTime);

    return timePtr;
}



static bool espnowMtxHndlr(void)
{
    bool status = true;

    if(xSemaphoreTake(xMtxEspnow, DEF_PEND))
    {
        gpio_intr_disable(INTR_PIN);
        xSemaphoreGive(xSemEspnow);
        giveSemLed();
    }
    else
    {
        ESP_LOGE(TAG2, "xMtxEspnow %s%s", mtxFail, rtrnNewLine);
        status = false;
    }

    return status;
}



char* printTime(cJSON *responsePtr)
{
    char *timeStr = NULL;
    size_t timeStrSize = 11;
    cJSON *serverTimePtr = cJSON_GetObjectItemCaseSensitive(responsePtr, "serverTime");
    struct tm *timePtr = setTime(serverTimePtr->valueint);

    timeStr = (char*) malloc(sizeof(char) * (timeStrSize));

    if(timeStr == NULL)
    {
        ESP_LOGE(TAG2, "Print time %s%s", mallocFail, rtrnNewLine);
        return timeStr;
    }

    if(!strftime(timeStr, timeStrSize, "0%H %M %S\r", timePtr))
    {
        ESP_LOGE(TAG2, "Time string size fail%s", rtrnNewLine);
        free(timeStr);
        timeStr = NULL;
    }

    return timeStr;
}



char* printValid(cJSON *responsePtr)
{
    char *validStr = NULL;
    char *placehldr = NULL;
    cJSON *responseCode = cJSON_GetObjectItemCaseSensitive(responsePtr, "responseCode");

    placehldr = ((responseCode->valueint) == 1) ? "23\r" : "24\r"; // FIXME (MAGIC NUMBER)

    validStr = (char*) malloc(sizeof(char) * (strlen(placehldr) + 1));
    if(validStr != NULL)
    {
        snprintf(validStr, strlen(placehldr) + 1, "%s", placehldr);
    }
    else
    {
        ESP_LOGE(TAG2, "Print Valid %s%s", mallocFail, rtrnNewLine);
    }

    return validStr;
}



static char* reserveTimesHndlr(cJSON *reserveTime)
{
    size_t timeStrSize = 8;
    char *reserveTimeStr = NULL;
    time_t rsvTime = reserveTime->valueint;

    reserveTimeStr = (char*) malloc(sizeof(char) * (timeStrSize));

    if(reserveTimeStr == NULL)
    {
        ESP_LOGE(TAG2, "Reserve string %s%s", mallocFail, rtrnNewLine);
        return reserveTimeStr;
    }

    if(!strftime(reserveTimeStr, timeStrSize, "%I:%M%p", localtime(&rsvTime)))
    {
        ESP_LOGE(TAG2, "Reserve string size fail%s", rtrnNewLine);
        free(reserveTimeStr);
        reserveTimeStr = NULL;
    }

    return reserveTimeStr;
}


// NOTE: This can be re-factored, but for the sake of time, we will leave it
char* printReserve(cJSON *responsePtr)
{
    size_t reserveStrSize = 26; // FIXME
    char *reserveStr = NULL;

    cJSON * namePtr = cJSON_GetObjectItemCaseSensitive(responsePtr, "firstName");
    cJSON *startTimeJsonPtr = cJSON_GetObjectItemCaseSensitive(responsePtr, "unixStartTime");
    cJSON *endTimeJsonPtr = cJSON_GetObjectItemCaseSensitive(responsePtr, "unixEndTime");

    if(!(cJSON_IsNumber(startTimeJsonPtr)) && 
        !(cJSON_IsNumber(endTimeJsonPtr)) && 
        !(cJSON_IsString(namePtr)))
    {
        reserveStr = (char*) malloc(sizeof(char) * (26)); // FIXME
        if(reserveStr != NULL)
        {
            snprintf(reserveStr, reserveStrSize, "1No%9s%s\r", "", noRsv); // FIXME
        }
        else
        {
            ESP_LOGE(TAG2, "No reservation %s%s", mallocFail, rtrnNewLine);
        }
        return reserveStr;
    }

    char *startTimeStr = NULL;
    char *endTimeStr = NULL;
    startTimeStr = reserveTimesHndlr(startTimeJsonPtr);

    if(startTimeStr == NULL)
    {
        return reserveStr;
    }

    endTimeStr = reserveTimesHndlr(endTimeJsonPtr);

    if(endTimeStr == NULL)
    {
        free(startTimeStr);
        return reserveStr;
    }

    reserveStrSize = snprintf(NULL, 0, "1Reserved:\n%s\n\n%s to %s\r", \
                                namePtr->valuestring, startTimeStr, endTimeStr);

    reserveStr = (char*) malloc(sizeof(char) * (reserveStrSize + 1));

    if(reserveStr != NULL)
    {
        snprintf(reserveStr, reserveStrSize + 1, "1Reserved:\n%s\n\n%s to %s\r", \
                namePtr->valuestring, startTimeStr, endTimeStr); // FIX ME (MAGIC NUMBER)
    }
    else
    {
        ESP_LOGE(TAG2, "Reservation %s%s", mallocFail, rtrnNewLine);
    }
    free(endTimeStr);
    free(startTimeStr);

    //uint64_t nextRsvTime = ((uint64_t)time(NULL) - endTimeJsonPtr->valueint) * MICRO_SEC_FACTOR;
    //timerRestart(1, nextRsvTime); // FIXME (MAGIC NUMBER)

    return reserveStr;
}



static void uartRxWorkHndlr(void)
{
    if (wifiCheckStatus())
    {
        return;
    }

    if(uxSemaphoreGetCount(xMtxEspnow))
    {
        uint8_t idVal = 2;
        queuingParseData(idVal);
    }
}



int32_t getAccessCode(void)
{
    int32_t accessCodeCpy = 0;

    if(xSemaphoreTake(xMtxAccessCode, DEF_PEND))
    {
        accessCodeCpy = accessCode;
        accessCode = 0; /* To prevent abuse/errors */
        xSemaphoreGive(xMtxAccessCode);
    }
    else
    {
        ESP_LOGE(TAG2, "xMtxAccessCode get() %s%s", mtxFail, rtrnNewLine);
    }

    xSemaphoreGive(xMtxParanoid);

    return accessCodeCpy;
}



static void setAccessCode(char *buffer)
{
    if(!xSemaphoreTake(xMtxParanoid, DEF_PEND))
    {
        ESP_LOGE(TAG2, "xMtxParanoid %s%s", mtxFail, rtrnNewLine);
        return;
    }
    
    if(strlen(buffer) != 7)
    {
        ESP_LOGE(TAG1, "Invalid Access Code%s", rtrnNewLine);
        return;
    }

    if(xSemaphoreTake(xMtxAccessCode, DEF_PEND))
    {
        accessCode = atol(buffer);
        xSemaphoreGive(xMtxAccessCode);
    }
    else
    {
        ESP_LOGE(TAG2, "xMtxAccessCode set() %s%s", mtxFail, rtrnNewLine);
    }
}



static void xUartRxTask(void *pvParameters)
{
    uart_event_t event;

    char* rxBuffer = (char*) malloc(BUF_SIZE);

    while(true)
    {
        xQueueReceive(xQueueUartRx, (void*) &event, portMAX_DELAY);

        switch(event.type)
        {
            case UART_DATA:
                uart_read_bytes(UART_PORT, rxBuffer, event.size, READ_DELAY);
                rxBuffer[event.size - 1] = '\0';
                setAccessCode(rxBuffer);
                uartRxWorkHndlr();
                break;
            
            case UART_FIFO_OVF:
                ESP_LOGE(TAG1, "Buffer Overflow%s", rtrnNewLine);
                uart_flush_input(UART_PORT);
                break;
            
            case UART_BUFFER_FULL:
                ESP_LOGE(TAG1, "Buffer Full%s", rtrnNewLine);
                uart_flush_input(UART_PORT);
                break;

            case UART_PARITY_ERR:
                ESP_LOGE(TAG1, "Parity Error%s", rtrnNewLine);
                break;
            
            case UART_FRAME_ERR:
                ESP_LOGE(TAG1, "Frame Error%s", rtrnNewLine);
                break;
            
            default:
                break;
        } /* End Switch Statement */


    }
}



static void xUartTxTask(void *pvParameters)
{
    while(true)
    {
        cJSON *responsePtr = NULL;

        xQueueReceive(xQueueUartTx, (void*) &responsePtr, portMAX_DELAY);
        giveSemUartTxGuard();

        cJSON *responseID = cJSON_GetObjectItemCaseSensitive(responsePtr, "id");

        char *toTxBuf = respPrintFuncs[responseID->valueint](responsePtr);
        if(toTxBuf != NULL)
        {
            ESP_LOGI(TAG2, "UART MSG: %s", toTxBuf);
            uart_write_bytes(UART_PORT, toTxBuf, strlen(toTxBuf) + 1);

            free(toTxBuf);

            cJSON *responseCode = cJSON_GetObjectItemCaseSensitive(responsePtr, \
                                                                    "responseCode");

            if((responseID->valueint == 2) && \
                (responseCode->valueint == 1)) // FIXME (MAGIC NUMBERS)
            {
                espnowMtxHndlr();
            }
        }
        cJSON_Delete(responsePtr);
    }
}