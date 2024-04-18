/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: main.h
** ----------
** Header file for the main.c. Provides constant
** values to be utilized throughout the program.
*/

#ifndef MAIN_H_
#define MAIN_H_

/* Standard Library Headers */
#include <stdio.h>

/* Driver Headers */
#include "sdkconfig.h"

/* Defines */
#define WIFI_DISC_BIT (1 << 0)

#define DEF_PEND pdMS_TO_TICKS(10)
#define SEC_DELAY pdMS_TO_TICKS(1000)
#define LOCK_DELAY pdMS_TO_TICKS(10000)

#define Q_CNT 3
#define SEM_CNT 3
#define PSEUDO_MTX_CNT 1

#define CORE1_PRIO 10
#define STACK_DEPTH 4096

/* Enum for Global Constant String Sizes */
typedef enum 
{
    NEWLINE_LEN = 3,
    MALLOC_LEN = 14,
    MTX_LEN = 25,
    HEAP_LEN = 28,

} glblStrLengths;

/* Enum for Local Constant TAG String Sizes */
typedef enum
{
    TAG_LEN_8 = 8,
    TAG_LEN_9,
    TAG_LEN_10,
} tagLengths;

/* Declaration of Global Constant Strings */
extern const char mallocFail[MALLOC_LEN];
extern const char rtrnNewLine[NEWLINE_LEN];
extern const char mtxFail[MTX_LEN];
extern const char heapFail[HEAP_LEN];

#endif /* MAIN_H_*/