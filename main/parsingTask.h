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
#define POST_STATE_SZ 3 /* Size for POST Request State-related Arrays */
#define SEND_FAIL_LEN 19
#define FULL_FAIL_LEN 9

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

/* Function Declarations */
extern void startParsingConfig(void);
extern void mallocCleanup(requestBodyData *reqPtr, int8_t mallocCnt);
extern void queuingParseData(uint8_t idVal);
extern void giveSemHttpGuard(void);

/* FreeRTOS Declared API Handles */
extern QueueHandle_t xQueueHttp;

/* Declaration of Global Constant Strings */
extern const char queueSendFail[SEND_FAIL_LEN];
extern const char queueFullFail[FULL_FAIL_LEN];

/* Enum for Heap Sizes */
typedef enum
{
    MAX_HEAP,
    HIGH_HEAP,
    LOW_HEAP,
    NO_HEAP,
} heapSize;

/* Typedefs for Pointer to Function and Function */
typedef bool (*parsingFuncPtr)(requestBodyData* reqPtr);
typedef bool (parsingFunc)(requestBodyData* reqPtr);

#endif /* PARSINGTASK_H_*/