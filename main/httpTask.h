/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: httpTask.h
** ----------
** ......
*/

#ifndef HTTPTASK_H_
#define HTTPTASK_H_

/* Standard Library Headers */
#include <stdio.h>

/* Driver Headers */
#include "esp_timer.h"

/* Local Headers */
#include "parsingTask.h"



/* Defines */
#define HTTP_PRIO 19 /* Priority of Task */
/* NOTE:
** Lowest priority of all the Wi-Fi related tasks since
** all other tasks on Core 0 are either determining if
** connection is currently viable or provide communication
** processes that are necessary for safe operation.
*/

#define DEF_FAIL_TOUT 20000000 /* Time in microseconds */

/* Function Declarations */
extern void startHttpConfig(void);
extern void timerRestart(uint8_t timerNum, uint64_t timeout);
extern void giveSemUartTxGuard(void);

/* FreeRTOS Declared API Handles */
extern QueueHandle_t xQueueUartTx;

/* Enum for POST Response, Response Code Values */
typedef enum
{
    VALID_RESP = 1,
    INVALID_RESP,
    NO_RSV_RESP = 7,
} respCodeVals;

/* Enum for POST Request/Response ID Values */
typedef enum
{
    TIME_ID, /* ID values correlate to Timer ID values */
    RSV_ID,
    CODE_ID,
} respIdVals;

/* Typedef Struct for ESP-IDF Timers */
typedef struct 
{
    uint8_t timerNum;
    esp_timer_handle_t* timerHndl;
} timerArgsStruct;

#endif /* HTTPTASK_H_*/