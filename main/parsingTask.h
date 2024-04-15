/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: parsingTasks.h
** ----------
** ......
*/

#ifndef PARSINGTASK_H_
#define PARSINGTASK_H_

/* Standard Library Headers */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

/* Defines */
#define QUICK_DELAY pdMS_TO_TICKS(1000)
#define BASE_RSV_LEN 21
#define BASE_CODE_LEN 18
#define ACCESS_LEN 7


extern QueueHandle_t xQueueHttp;

/* Typedef Struct for HTTP Request Body Data */
typedef struct 
{
    uint8_t id;
    char* url;
    size_t urlLen;
    char* jsonStr;
    size_t jsonStrLen;
    int32_t accessCode;
} requestBodyData;

/* Enum for Heap Sizes */
typedef enum
{
    MAX_HEAP,
    HIGH_HEAP,
    LOW_HEAP,
    NO_HEAP,
} heapSize;

typedef bool (*parsingFuncPtr)(requestBodyData* reqPtr);
typedef bool (parsingFunc)(requestBodyData* reqPtr);

extern void startParsingConfig(void);
extern void mallocCleanup(requestBodyData *reqPtr, int8_t mallocCnt);
extern void queuingParseData(uint8_t idVal);
extern void giveSemHttpGuard(void);

#endif /* PARSINGTASK_H_*/