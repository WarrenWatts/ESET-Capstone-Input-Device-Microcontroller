/* Texas A&M University
** Electronic Systems Engineering Technology
** ESET-420 Engineering Technology Capstone II
** Author: Warren Watts
** File: espnowTask.c
** --------
** Configures ESP-NOW functionality and creates a task to send
** ESP-NOW frames. A callback function was also created to handle
** "alive" status frames.
*/

/* Standard Library Headers */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* RTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* Driver Headers */
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_err.h"
#include "esp_log.h"

/* Local Headers */
#include "ledTask.h"
#include "wifiTask.h"
#include "main.h"
#include "espnowTask.h"



/* Variable Naming Abbreviations Legend:
**
** Sem - Semaphore
** Mtx - Mutex
** Rtrn - Return
** Len - Length
** Msg - Message
** Chnl - Channel
** Rcv - Receive
** Def - Default
** 
*/



/* Local Defines */
#define MSG_LEN 2

/* Local Function Declarations */
static void espnowRtosConfig(void);
static void espnowPeerConfig(void);
static void xEspnowTask(void *pvParameters);
void espnowRcvCallback(const esp_now_recv_info_t* espNowInfo, const uint8_t *myData, int dataLen);

/* FreeRTOS Local API Handles */
static SemaphoreHandle_t xMtxRcvInfo;

/* FreeRTOS Defining API Handles */
SemaphoreHandle_t xMtxEspnow;
SemaphoreHandle_t xSemEspnow;

/* ESPNOW Peer Handle */
static esp_now_peer_info_t receiverInfo;

/* Local Constant Logging String */
static const char TAG[TAG_LEN_8] = "ESP_NOW";

/* Peer Receiver MAC Address String */
static const uint8_t receiverMAC[ESP_NOW_ETH_ALEN] = {0x7C, 0xDF, 0xA1, 0xE5, 0x44, 0x30};



/* The startEspnowConfig() function is used to initialize ESP-NOW
** functionality for the ESP32.
**
** Parameters:
**  none
**
** Return:
**  none
*/
void startEspnowConfig(void)
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

    ESP_ERROR_CHECK(esp_now_init());
    esp_now_register_recv_cb(espnowRcvCallback);
    espnowRtosConfig();
    espnowPeerConfig();
}



/* The startEspnowRtosConfig() function is used to initialize the
** Semaphores and Mutexes that are used by the xEspnowTask and its
** associated functions. The xEspnowTask is also created here.
**
** Parameters:
**  none
**
** Return:
**  none
*/
static void espnowRtosConfig(void)
{
    if(!(xMtxRcvInfo = xSemaphoreCreateMutex()))
    {
        ESP_LOGE(TAG, "%s xMtxRcvInfo%s", heapFail, rtrnNewLine);
    }

    if(!(xMtxEspnow = xSemaphoreCreateCounting(PSEUDO_MTX_CNT, PSEUDO_MTX_CNT)))
    {
        ESP_LOGE(TAG, "%s xMtxEspnow%s", heapFail, rtrnNewLine);
    }

    if(!(xSemEspnow = xSemaphoreCreateBinary()))
    {
        ESP_LOGE(TAG, "%s xSemEspnow%s", heapFail, rtrnNewLine);
    }

    xTaskCreatePinnedToCore(&xEspnowTask, "ESPNOW_TASk", STACK_DEPTH, 0, NOW_PRIO, 0, 0);
}



/* The espnowPeerConfig() function is used to both
** initialize and modify the peer configuration. More
** specifically, the modification of the Wi-Fi channel that
** the device is transmitting ESP-NOW frames to.
**
** Parameters:
**  none
**
** Return:
**  none
**
** Notes: For transmission to be successful between two ESPs,
** they must be transmitted/receiving on the same Wi-Fi channel.
** It is also the case that the Wi-Fi channel being used currently
** to transmit must match the Wi-Fi channel you have configured
** for your peer to receive on, otherwise an error will occur.
** Since Wi-Fi networks can be dynamic in nature, changing the
** channels that a device is communicating on forcefully, a singular
** function that handled both the initial configuration of the peer
** and its modification was created.
*/
static void espnowPeerConfig(void)
{
    if(xSemaphoreTake(xMtxRcvInfo, DEF_PEND))
    {
        static bool peerInitialized = false;

        uint8_t primaryChnl = 0;
        wifi_second_chan_t secondaryChnl = 0;

        esp_wifi_get_channel(&primaryChnl, &secondaryChnl);
        receiverInfo.channel = primaryChnl;

        if(!peerInitialized)
        {
            peerInitialized = true;

            receiverInfo.ifidx = ESP_IF_WIFI_STA;
            receiverInfo.encrypt = false;
            memcpy(receiverInfo.peer_addr, receiverMAC, ESP_NOW_ETH_ALEN);

            esp_now_add_peer(&receiverInfo);
        }
        else
        {
            esp_now_mod_peer(&receiverInfo);
        }
        xSemaphoreGive(xMtxRcvInfo);
    }
    else
    {
        ESP_LOGE(TAG, "xMtxRcvInfo %s%s", mtxFail, rtrnNewLine);
    }
}



/* The espnowRcvCallback() function handles the frame data
** sent from the Locking Device Microcontroller. (Alive status frames.)
** A confirmation frame is sent if the "alive" status frame is received successfully.
**
** Parameters:
**  espnowInfo - ESP-NOW packet information struct
**  myData - string data received from ESP-NOW frame
**  dataLen - length of the data
**
** Return:
**  none
** 
** Notes: The peer configuration is only reset if the current transmitting channel
** does not align with the current peer configuration channel.
*/
void espnowRcvCallback(const esp_now_recv_info_t* espNowInfo, const uint8_t *myData, int dataLen)
{
    const uint8_t msgData[MSG_LEN] = "2";
    if(esp_now_send(receiverMAC, msgData, MSG_LEN) == ESP_ERR_ESPNOW_ARG) /* Mis-aligned channels error */
    {
        espnowPeerConfig();
    }
}



/* The xEspnowTask() function sends an ESP-NOW frame to the
** Locking Device Microcontroller telling it to engage the Solenoid.
** It pends on a synchronization Semaphore and implements redundancy checks
** by attempting to send multiple times. If an error occurs such that the
** channels are mis-aligned, they will be corrected. All other errors are logged.
**
** Parameters:
**  none used
**
** Return:
**  none
**
** Notes: Regardless of ending in success or failure, the single ISR this program
** uses will be re-enabled at the end of this function and the Pseudo-Mutex 
** (Counting Semaphore) guarding the use of the xEspnowTask will be returned.
*/
static void xEspnowTask(void *pvParameters)
{
    while(true)
    {
        xSemaphoreTake(xSemEspnow, portMAX_DELAY);

        uint8_t count;
        int16_t result = 0;
        const uint8_t msgData[MSG_LEN] = "1";

        for(count = 0; count < MAX_ATMPT; count++)
        {
            if((result = esp_now_send(receiverMAC, msgData, MSG_LEN)) == ESP_OK)
            {
                break;
            }

            ESP_LOGE(TAG, "Error No: %d%s", result, rtrnNewLine);

            if(result == ESP_ERR_ESPNOW_ARG) /* Mis-aligned channels error */
            {
                espnowPeerConfig();
            }
            
            vTaskDelay(FIRST_11_DELAY);
        }
        gpio_intr_enable(INTR_PIN);
        /* Pseudo-Mutex to Guard ESP-NOW Task Use */
        xSemaphoreGive(xMtxEspnow);
    }
}