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

#define HTTP_PRIO 19

#define HTTP_TOUT 250
#define RES_DEF_TOUT 60000000
#define TIME_DEF_TOUT 86400000000

#define RES_FAIL_TOUT 20000000
#define TIME_FAIL_TOUT 15000000
#define DEF_FAIL_TOUT 20000000

extern void startHttpConfig(void);
extern void timerRestart(uint8_t timerNum, uint64_t timeout);
extern void giveSemUartTxGuard(void);


extern QueueHandle_t xQueueUartTx;

/* Typedef Struct for ESP-IDF Timers */
typedef struct 
{
    uint8_t timerNum;
    esp_timer_handle_t* timerHndl;
} timerArgsStruct;

#endif /* HTTPTASK_H_*/