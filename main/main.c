/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: main.c
** --------
** Main function and configuring of all ESP and
** FreeRTOS functionality.
*/

/* Standard Library Headers */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* Driver Headers */
#include "esp_log.h"
#include "esp_err.h"

/* Local Headers */
#include "main.h"
#include "wifiTask.h"
#include "espnowTask.h"
#include "parsingTask.h"
#include "httpTask.h"
#include "uartTasks.h"
#include "ledTask.h"



/* Defining Declarations of Global Constant Strings */
const char mallocFail[MALLOC_SZ] = "Malloc failed";
const char heapFail[HEAP_SZ] = "Insufficient heap space for";
const char mtxFail[MTX_SZ] = "Mutex failed to take key";
const char rtrnNewLine[NEWLINE_SZ] = "\r\n";

/* Static Function Declarations */
static void startTzConfig(void);



/* The app_main() function acts as the main() function for
** ESP-IDF based projects. This function is utilized to 
** configure all necessary ESP and FreeRTOS functionality
** for this program.
**
** Parameters:
**  none
**
** Return:
**  none
*/
void app_main(void)
{
    startTzConfig();
    startWifiConfig();
    startEspnowConfig();
    startParsingConfig();
    startHttpConfig();
    startUartConfig();
    startPinConfig();
}



/* The startTzConfig() function simply sets the ESP's 
** timezone to a specified value.
**
** Parameters:
**  none
**
** Return:
**  none
*/
static void startTzConfig(void)
{
    setenv("TZ", "CDT+5", 1);
    tzset();
}
