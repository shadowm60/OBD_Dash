#ifndef _CAN_H_
#define _CAN_H_
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"

#define TX_GPIO_NUM     CONFIG_CAN_TX
#define RX_GPIO_NUM     CONFIG_CAN_RX

#define RX_QUEUE_SIZE   10 //number of messages to be stored
#define TX_QUEUE_SIZE   10 //number of messages to be stored


#define RX_TASK_PRIO            8
#define TX_TASK_PRIO            9
#define PROC_TSK_PRIO           10


//#define CAN_BLOCK_MAX portMAX_DELAY
#define CAN_BLOCK_MAX ((portTICK_PERIOD_MS ) /2)    //max 1ms/10

//isotp defines
#define ISOTP_BUFSIZE   128
//all these will transact true iso-tp even for Single frame communication
//TX ID
#define TESTER_CAN_ID       0x7DF   //functional id, all ECUs will receive this
//RX IDs
#define ECU_CAN_ID          0x7E8   //physical Engine control unit ID
#define TCU_CAN_ID          0x7E9   //physical Transmission control unit ID

void init_can( void );
void init_can_tasks( void );
void can_send_message(twai_message_t *msg);
void can_send_isotp_message( uint8_t *data, uint8_t len);
int can_get_isotp_message(uint8_t *payload, const uint16_t payload_size, uint16_t *out_size);
uint32_t can_get_timestamp_ms ( void );

#endif