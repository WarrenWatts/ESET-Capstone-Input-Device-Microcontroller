/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: wifiTask.h
** ----------
** ......
*/

#ifndef WIFITASK_H_
#define WIFITASK_H_

/* Standard Library Headers */
#include <stdio.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Defines */
#define SSID "Warren iPhone"
#define PASS "Warren123"

#define MAX_ATMPT 11
#define DFLT_CHNL 11
#define WIFI_PRIO 19

#define FIRST_11_DELAY pdMS_TO_TICKS(20)
#define DEF_DELAY pdMS_TO_TICKS(1000)

/* Function Declarations */
extern void startWifiConfig(void);
extern UBaseType_t wifiCheckStatus(void);

#endif /* WIFITASK_H_ */