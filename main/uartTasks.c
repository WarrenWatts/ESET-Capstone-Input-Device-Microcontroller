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
** Tx - Transmit
** Rx - Receive
** Rsvtion - Reservation
** Resp - Response
** Intr - Interrupt
** Hldr - Holder
** Hndlr - Handler
**
*/



/* Local Defines */
#define TX_BUF_SZ 4
#define RX_BUF_SZ 256

#define UART_PORT 1
#define UART_BAUD_RATE 115200

#define TX_PIN 17
#define RX_PIN 18
#define RTS_PIN (UART_PIN_NO_CHANGE)
#define CTS_PIN (UART_PIN_NO_CHANGE)

#define MICRO_SEC_FACTOR 1000000

/* Enum for Local Constant String Sizes */
typedef enum
{
    CODE_LEN = 7, /* Access Code String Size () */
    RSV_TIME_LEN, /* Size of Meridiem Time String */
    TIME_LEN = 11, /* Size of UART Passed Time String */
    RSVTION_LEN,
    NO_RSV_LEN = 26,
} localStrLengths;

/* Local Function Declarations */
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

/* FreeRTOS Local API Handles */
static SemaphoreHandle_t xMtxTimeBool;
static SemaphoreHandle_t xMtxAccessCode;
static SemaphoreHandle_t xMtxParanoid;
static QueueHandle_t xQueueUartRx;

/* FreeRTOS Reference API Handles */
extern QueueHandle_t xQueueUartTx;
extern SemaphoreHandle_t xMtxEspnow;
extern SemaphoreHandle_t xSemEspnow;

/* Reference Declarations of Global Constant Strings */
extern const char mallocFail[MALLOC_LEN];
extern const char heapFail[HEAP_LEN];
extern const char mtxFail[MTX_LEN];
extern const char rtrnNewLine[NEWLINE_LEN];

/* Local String Constants */
static const char TAG1[TAG_LEN_8] = "UART_RX";
static const char TAG2[TAG_LEN_8] = "UART_TX";
static const char noRsvStr[RSVTION_LEN] = "Reservation";


/* Boolean for System Time if Set */
static bool timeSetBool = false;

/* Value of Access Code in Integer Form */
static int32_t accessCode = 0;

/* Constant State Array of Pointers to Functions */
printingFuncPtr respPrintFuncs[POST_STATE_SZ] =
{
    printTime,      // ID: 0
    printReserve,   // ID: 1
    printValid      // ID: 2
};



/* The startUartConfig() function is used to initialize UART
** functionality for this ESP device. (Both TX and RX UART 
** are configured.)
**
** Parameters:
**  none
**
** Return:
**  none
*/
void startUartConfig(void)
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

    uart_config_t uartConfig = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intrAllocFlags = 0;

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, RX_BUF_SZ, 0, TX_BUF_SZ, &xQueueUartRx, intrAllocFlags));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uartConfig));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, TX_PIN, RX_PIN, RTS_PIN, CTS_PIN));

    startUartRtosConfig();
}



/* The startUartRtosConfig() function is used to initialize the
** Semaphores and Mutexes that are used by the UART tasks and their
** associated functions. The two UART tasks, xUartRxTask and xUartTxTask,
** are also created here.
**
** Parameters:
**  none
**
** Return:
**  none
*/
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


/* The printTime() function takes the UNIX server time value and formats it
** into a string, that can be interpreted by touchscreen, to be sent via UART.
** The string is formatted into Hours, Minutes, and Seconds, with a leading value
** of zero, which acts as a header byte for the data when read on the touchscreen.
** The '\r' acts as the trailing byte.
**
** Parameters:
**  responsePtr - pointer to the dynamically allocated cJSON object
**
** Return:
**  A pointer to the dynamically allocated time string to be sent via UART
**
** Notes: Since the system time will only be reset once every twenty-four hours, we also
** call the setTime() function in this function.
*/
char* printTime(cJSON *responsePtr)
{
    char *timeStr = NULL;
    size_t timeStrLen = TIME_LEN;
    cJSON *serverTimePtr = cJSON_GetObjectItemCaseSensitive(responsePtr, "serverTime");
    struct tm *timePtr = setTime(serverTimePtr->valueint); /* time.h based struct tm */
    
    timeStr = (char*) malloc(sizeof(char) * (timeStrLen));

    if(timeStr == NULL)
    {
        ESP_LOGE(TAG2, "Print time %s%s", mallocFail, rtrnNewLine);
        return timeStr;
    }

    if(!strftime(timeStr, timeStrLen, "0%H %M %S\r", timePtr))
    {
        ESP_LOGE(TAG2, "Time string size fail%s", rtrnNewLine);
        free(timeStr);
        timeStr = NULL;
    }

    return timeStr;
}



/* The printValid() function takes the access code validation value and formats it
** in a way that is readable to the touchscreen. Again we add a header byte ('2' for
** access codes) and a trailer byte of '\r'. The value sandwiched between represents a 
** success or a failure ('3' being success and '4' being failure).
**
** Parameters:
**  responsePtr - pointer to the dynamically allocated cJSON object
**
** Return:
**  A pointer to the dynamically allocated acces code validation string to be sent via UART
*/
char* printValid(cJSON *responsePtr)
{
    char *validStr = NULL;
    char *placehldr = NULL;
    cJSON *responseCode = cJSON_GetObjectItemCaseSensitive(responsePtr, "responseCode");

    placehldr = ((responseCode->valueint) == VALID_RESP) ? "23\r" : "24\r";

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



/* The printReserve() function takes the information from the reserve POST Response
** and formats it in a way that is readable to both the touchscreen and the user,
** since this will be displayed directly on the touchscreen itself. Again we add a header byte ('1' for
** reservation text) and a trailer byte of '\r'. The unix start time and unix end time for the
** reservation must be formatted into meridiem based time, e.g., 9:30pm. Due to adding error recovery/logging
** for these cases (among others), the size of this function is much larger than the other two
** print functions.
**
** Parameters:
**  responsePtr - pointer to the dynamically allocated cJSON object
**
** Return:
**  A pointer to the dynamically allocated reservation text string to be sent via UART
** 
** Notes: If there is no reservation at the time, a default piece of text (handled within
** the first if statement) is sent instead. Also, if there is no reservation, the reservation
** info timer will continue to time out every minute until a reservation (that is occurring currently)
** has been placed. Otherwise, the timer will not timeout until the end of the current reservation.
*/
char* printReserve(cJSON *responsePtr)
{
    size_t reserveStrLen = NO_RSV_LEN;
    char *reserveStr = NULL;

    cJSON * namePtr = cJSON_GetObjectItemCaseSensitive(responsePtr, \
                                                        "firstName");

    cJSON *startTimeJsonPtr = cJSON_GetObjectItemCaseSensitive(responsePtr, \
                                                                "unixStartTime");

    cJSON *endTimeJsonPtr = cJSON_GetObjectItemCaseSensitive(responsePtr, \
                                                                "unixEndTime");

    if(!(cJSON_IsNumber(startTimeJsonPtr)) && \
        !(cJSON_IsNumber(endTimeJsonPtr)) && \
        !(cJSON_IsString(namePtr)))
    {
        reserveStr = (char*) malloc(sizeof(char) * (reserveStrLen));
        if(reserveStr != NULL)
        {
            snprintf(reserveStr, reserveStrLen, "1No%9s%s\r", "", noRsvStr);
        }
        else
        {
            ESP_LOGE(TAG2, "No %s %s%s", noRsvStr, mallocFail, rtrnNewLine);
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

    /* Doing this to ensure sufficient space is allocated for strings */
    reserveStrLen = snprintf(NULL, 0, "1Reserved:\n%s%9s\n%s to %s\r", \
                            namePtr->valuestring, "", startTimeStr, endTimeStr);

    reserveStr = (char*) malloc(sizeof(char) * (reserveStrLen + 1));

    if(reserveStr != NULL)
    {
        snprintf(reserveStr, reserveStrLen + 1, "1Reserved:\n%s%9s\n%s to %s\r", \
                namePtr->valuestring, "", startTimeStr, endTimeStr);
        
        /* Cause a timeout at the end of the current reservation */
        uint64_t nextRsvTime = ((endTimeJsonPtr->valueint) - time(NULL)) * MICRO_SEC_FACTOR;
        timerRestart(RSV_ID, nextRsvTime);
    }
    else
    {
        ESP_LOGE(TAG2, "Reservation %s%s", mallocFail, rtrnNewLine);
    }
    free(endTimeStr);
    free(startTimeStr);

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
        uint8_t idVal = CODE_ID;
        queuingParseData(idVal);
    }
}



static char* reserveTimesHndlr(cJSON *reserveTime)
{
    size_t timeStrLen = RSV_TIME_LEN;
    char *reserveTimeStr = NULL;
    time_t rsvTime = reserveTime->valueint;

    reserveTimeStr = (char*) malloc(sizeof(char) * (timeStrLen));

    if(reserveTimeStr == NULL)
    {
        ESP_LOGE(TAG2, "Reserve string %s%s", mallocFail, rtrnNewLine);
        return reserveTimeStr;
    }

    if(!strftime(reserveTimeStr, timeStrLen, "%I:%M%p", localtime(&rsvTime)))
    {
        ESP_LOGE(TAG2, "Reserve string size fail%s", rtrnNewLine);
        free(reserveTimeStr);
        reserveTimeStr = NULL;
    }

    return reserveTimeStr;
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



/* The getAccessCode() function is the getter function for the
** accessCode variable.
**
** Parameters:
**  none
**
** Return:
**  An integer of the access code value
**
** Notes: The access code is always reset to zero after being copied to
** prevent the previous access code from being held and used at a later point.
** Further note that the xMtxParanoid Pseudo-Mutex is given back here. This Pseudo-Mutex is used
** to guard the overall operation of getting and setting the access code. This prevents the
** access code from being set again before we obtain the actual value from the getter function.
*/
int32_t getAccessCode(void)
{
    int32_t accessCodeCpy = 0;

    /* Mutex to Guard accessCode Variable */
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



/* The setAccessCode() function is the setter function for the
** accessCode variable, a global variable that stores the access
** code received as a string from UART as an integer value.
**
** Parameters:
**  buffer - pointer to the UART provided access code string
**
** Return:
**  none
*/
static void setAccessCode(char *buffer)
{
    /* Mutex to Guard Access Code Operation */
    if(!xSemaphoreTake(xMtxParanoid, DEF_PEND))
    {
        ESP_LOGE(TAG2, "xMtxParanoid %s%s", mtxFail, rtrnNewLine);
        return;
    }
    
    if(strlen(buffer) != CODE_LEN)
    {
        ESP_LOGE(TAG1, "Invalid Access Code%s", rtrnNewLine);
        return;
    }

    /* Mutex to Guard accessCode Variable */
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



/* The getTimeBool() function is the getter function for the
** timeSetBool variable.
**
** Parameters:
**  none
**
** Return:
**  Boolean that signifies whether the server time is set or not
*/
bool getTimeBool(void)
{
    bool localTimeSetBool = false;

    /* Mutex to Guard timeSetBool Variable */
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



/* The setTimeBool() function is the setter function for the
** timeSetBool variable.
**
** Parameters:
**  localTimeSetBool - the Boolean value to set the timeSetBool variable
**
** Return:
**  none
*/
static void setTimeBool(bool localTimeSetBool)
{
    /* Mutex to Guard timeSetBool Variable */
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



/* The setTime() function is the setter function for the
** time of the system based on the received UNIX server time
** value.
**
** Parameters:
**  serverTime - an integer value holding the UNIX server time
**
** Return:
**  none
** 
** Notes: Using basic type since cJSON object only specifies
** an integer type and nothing more.
*/
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



static void xUartRxTask(void *pvParameters)
{
    uart_event_t event;

    char* rxBuffer = (char*) malloc(RX_BUF_SZ);

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

            if((responseID->valueint == CODE_ID) && \
                (responseCode->valueint == VALID_RESP))
            {
                espnowMtxHndlr();
            }
        }
        cJSON_Delete(responsePtr);
    }
}