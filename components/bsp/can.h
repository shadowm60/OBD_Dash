#ifndef _CAN_H_
#define _CAN_H_
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
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
// Functional broadcast TX ID (Single-Frame requests only)
#define TESTER_CAN_ID       0x7DF
// Physical Request IDs (Tester -> ECU, used for Flow Control and direct physical requests)
#define ECU_REQ_CAN_ID      0x7E0   // Engine ECU physical request (0x7E8 - 8)
#define TCU_REQ_CAN_ID      0x7E1   // Transmission ECU physical request (0x7E9 - 8)
// Physical Response IDs (ECU -> Tester)
#define ECU_CAN_ID          0x7E8   // Engine ECU response
#define TCU_CAN_ID          0x7E9   // Transmission ECU response

void init_can( void );
void init_can_tasks( void );
void can_send_message(twai_message_t *msg);
void can_send_isotp_message( uint8_t *data, uint8_t len);
int can_get_isotp_message(uint8_t *payload, const uint16_t payload_size, uint16_t *out_size);
uint32_t can_get_timestamp_ms ( void );

#endif