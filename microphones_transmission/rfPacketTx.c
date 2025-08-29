/*
Based on rfPacketTx example
 */

/***** Includes *****/
/* Standard C Libraries */
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

/* TI Drivers */
#include <ti/drivers/rf/RF.h>
//#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>

#include <ti/drivers/GPIO.h>
#include <ti/drivers/Power.h>
#include <ti/devices/cc13x0/driverlib/aon_batmon.h>

/* Driverlib Header files */
#include DeviceFamily_constructPath(driverlib/rf_prop_mailbox.h)

/* Board Header files */
#include "scif.h"
#include "Board.h"
#include "smartrf_settings/smartrf_settings.h"

/***** Defines *****/

/* Do power measurement */
//#define POWER_MEASUREMENT

/* Packet TX Configuration */
#define PAYLOAD_LENGTH              20
#define THREADSTACKSIZE             1024
#define MIN_TO_SEC                  60
#define TRANSMIT                    1
#define NUM_ADC_SAMPLES_PER_SEC     4  // Match your SC Studio setting



/***** Buffers for two microphones *****/
uint32_t mic0_buffer[SCIF_READING_BUFFER_SIZE];
uint32_t mic1_buffer[SCIF_READING_BUFFER_SIZE];


/***** Prototypes *****/

/***** Variable declarations *****/
static RF_Object rfObject;
static RF_Handle rfHandle;

uint8_t packet[PAYLOAD_LENGTH + 2];
static uint16_t seqNumber;
PIN_Handle pinHandle;


// ==== RF transmit helper ====
void send_databuffer(const void* buffer, int buffer_size) {
    RF_cmdPropTx.pktLen = buffer_size + 2;
    RF_postCmd(rfHandle, (RF_Op*)&RF_cmdFs, RF_PriorityNormal, NULL, 0);

    uint8_t *a = (uint8_t*)buffer;
    packet[0] = (uint8_t)(seqNumber >> 8);
    packet[1] = (uint8_t)(seqNumber++);

    uint8_t i;
    for ( i = 2; i < buffer_size + 2; i++) packet[i] = a[i - 2];

    RF_EventMask reason = RF_runCmd(rfHandle, (RF_Op*)&RF_cmdPropTx, RF_PriorityNormal, NULL, 0);
    if (!(reason & RF_EventLastCmdDone)) while(1);
    if (((volatile RF_Op*)&RF_cmdPropTx)->status != PROP_DONE_OK) while(1);

    RF_yield(rfHandle);
}


// SCIF driver callback: Task control interface ready (non-blocking task control operation completed)
void scCtrlReadyCallback(void) {

}

// SCIF driver callback: Sensor Controller task code has generated an alert interrupt
void scTaskAlertCallback(void) {

}

/***** Function definitions *****/

void *masterThread(void *arg0)
{
    RF_Params rfParams;
    RF_Params_init(&rfParams);

//    /* Open LED pins */
//    ledPinHandle = PIN_open(&ledPinState, pinTable);
//    if (ledPinHandle == NULL)
//    {
//        while(1);
//    }

    RF_cmdPropTx.pktLen = PAYLOAD_LENGTH + 2;
    RF_cmdPropTx.pPkt = packet;
    RF_cmdPropTx.startTrigger.triggerType = TRIG_NOW;

    /* Request access to the radio */
    rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioDivSetup, &rfParams);

    /* Set the frequency */
    RF_postCmd(rfHandle, (RF_Op*)&RF_cmdFs, RF_PriorityNormal, NULL, 0);
    Power_enablePolicy();

    while(1)
    {

        uint16_t head = scifTaskData.reading.output.head;
        uint16_t index = head;
        uint16_t ref_index = scifTaskData.reading.output.index;
        printf("The reference index is: %u\n", ref_index);
        int i;
//        printf(head);
        for (i = 0; i < SCIF_READING_BUFFER_SIZE; i++) {
            index = (index + 1) % SCIF_READING_BUFFER_SIZE;
//            uint16_t* samples0 = scifTaskData.reading.output.samples0;
            uint16_t value0 = scifTaskData.reading.output.samples0[index];
            uint16_t value1 = scifTaskData.reading.output.samples1[index];
            mic0_buffer[i] = value0;
            mic1_buffer[i] = value1;
        }

        uint8_t tx_buffer[PAYLOAD_LENGTH];

        int chunk_count = SCIF_READING_BUFFER_SIZE * sizeof(uint32_t) / PAYLOAD_LENGTH;

        for (i = 0; i < chunk_count; i++) {
            memcpy(tx_buffer, ((uint8_t*)mic0_buffer) + i * PAYLOAD_LENGTH, PAYLOAD_LENGTH);
            send_databuffer(tx_buffer, PAYLOAD_LENGTH);

            memcpy(tx_buffer, ((uint8_t*)mic1_buffer) + i * PAYLOAD_LENGTH, PAYLOAD_LENGTH);
            send_databuffer(tx_buffer, PAYLOAD_LENGTH);
        }


//        usleep((SCIF_READING_BUFFER_SIZE * 1000000) / NUM_ADC_SAMPLES_PER_SEC);
        sleep(1);
    }
}

// ==== Main thread start ====
void *mainThread(void *arg0) {
    pthread_t thread0;
    pthread_attr_t attrs;
    struct sched_param priParam;
    int retc;

    GPIO_init();
    PIN_State pinState;
    pinHandle = PIN_open(&pinState, BoardGpioInitTable);
//    PIN_setOutputValue(pinHandle, IOID_8, 1);

    Power_init();
    Power_enablePolicy();

    pthread_attr_init(&attrs);
    pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attrs, THREADSTACKSIZE);
    priParam.sched_priority = 1;
    pthread_attr_setschedparam(&attrs, &priParam);


    retc = pthread_create(&thread0, &attrs, masterThread, NULL);
    if (retc != 0) while (1);



    return NULL;
}
