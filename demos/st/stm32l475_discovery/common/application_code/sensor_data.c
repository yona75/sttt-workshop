/**
  ******************************************************************************
  * @file    sensor_data.c
  * @author  MCD Application Team
  * @version V1.0.0
  * @date    13-Feb-2019
  * @brief   A simple example demonstration how to publish sensor data to AWS.
  *
  *         It creates an MQTT client that publishes sensor data to the MQTT topic
  *         at regular intervals.
  *
  *         The user can subscribe to "freertos/demos/sensors/<thing_name>" topic
  *         from the AWS IoT Console to see the sensor reports as they arrive.
  *
  *         The demo uses a single task. The task implemented by
  *         prvMQTTConnectAndPublishTask() creates the MQTT client, subscribes to
  *         the broker specified by the clientcredentialMQTT_BROKER_ENDPOINT
  *         constant, performs the publish operations, and cleans up all the used
  *         resources after a minute of operation.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics International N.V.
  * All rights reserved.</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without
  * modification, are permitted, provided that the following conditions are met:
  *
  * 1. Redistribution of source code must retain the above copyright notice,
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of other
  *    contributors to this software may be used to endorse or promote products
  *    derived from this software without specific written permission.
  * 4. This software, including modifications and/or derivative works of this
  *    software, must execute solely and exclusively on microcontroller or
  *    microprocessor devices manufactured by or for STMicroelectronics.
  * 5. Redistribution and use of this software other than as permitted under
  *    this license is void and will automatically terminate your rights under
  *    this license.
  *
  * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
  * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
  * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT
  * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */

/* Standard includes. */
#include "string.h"
#include "stdio.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

/* MQTT includes. */
#include "aws_mqtt_agent.h"

/* Credentials includes. */
#include "aws_clientcredential.h"

/* Demo includes. */
#include "aws_demo_config.h"
#include "stm32l475e_iot01_accelero.h"
#include "stm32l475e_iot01_psensor.h"
#include "stm32l475e_iot01_gyro.h"
#include "stm32l475e_iot01_hsensor.h"
#include "stm32l475e_iot01_tsensor.h"
#include "stm32l475e_iot01_magneto.h"

/**
 * @brief MQTT client ID.
 *
 * It must be unique per MQTT broker.
 */
#define pubCLIENT_ID          (  ( const uint8_t * ) clientcredentialIOT_THING_NAME )

/**
 * @brief The topic that the MQTT client both subscribes and publishes to.
 */
#define pubTOPIC_NAME ( ( const uint8_t * ) "freertos/demos/sensors/" clientcredentialIOT_THING_NAME)

/**
 * @brief Dimension of the character array buffers used to hold data (strings in
 * this case) that is published to and received from the MQTT broker (in the cloud).
 */
#define pubMAX_DATA_LENGTH    256

/* MQTT publish task parameters. */
#define democonfigMQTT_PUB_TASK_STACK_SIZE                  ( 512 ) /* Stack size in words */
#define democonfigMQTT_PUB_TASK_PRIORITY                    ( tskIDLE_PRIORITY )
#define democonfigSAMPLING_DELAY_SECONDS                    ( 5 )
#define democonfigITERATE_FOREVER                           ( 1 ) /* Set to zero to iterate for one minute only */

/* Timeout used when establishing a connection, which required TLS
 * negotiation. */
#define democonfigMQTT_PUB_TLS_NEGOTIATION_TIMEOUT          pdMS_TO_TICKS( 12000 )

/* Timeout used when performing MQTT operations that do not need extra time
 * to perform a TLS negotiation. */
#define democonfigMQTT_TIMEOUT                               pdMS_TO_TICKS( 2500 )

/*-----------------------------------------------------------*/

/**
 * @brief Implements the task that connects to and then publishes messages to the
 * MQTT broker.
 *
 * Messages are published every five seconds for a minute.
 *
 * @param[in] pvParameters Parameters passed while creating the task. Unused in our
 * case.
 */
static void prvMQTTConnectAndPublishTask( void * pvParameters );

/**
 * @brief Creates an MQTT client and then connects to the MQTT broker.
 *
 * The MQTT broker end point is set by clientcredentialMQTT_BROKER_ENDPOINT.
 *
 * @return pdPASS if everything is successful, pdFAIL otherwise.
 */
static BaseType_t prvCreateClientAndConnectToBroker( void );

/**
 * @brief Publishes the next message to the pubTOPIC_NAME topic.
 *
 * This is called every five seconds to publish the next message.
 *
 */
static void prvPublishNextMessage( void );

/*-----------------------------------------------------------*/

/**
 * @ brief The handle of the MQTT client object
 */
static MQTTAgentHandle_t xMQTTHandle = NULL;

/*-----------------------------------------------------------*/

static BaseType_t prvCreateClientAndConnectToBroker( void )
{
    MQTTAgentReturnCode_t xReturned;
    BaseType_t xReturn = pdFAIL;
    MQTTAgentConnectParams_t xConnectParameters =
    {
        clientcredentialMQTT_BROKER_ENDPOINT, /* The URL of the MQTT broker to connect to. */
        democonfigMQTT_AGENT_CONNECT_FLAGS,   /* Connection flags. */
        pdFALSE,                              /* Deprecated. */
        clientcredentialMQTT_BROKER_PORT,     /* Port number on which the MQTT broker is listening. */
        pubCLIENT_ID,                         /* Client Identifier of the MQTT client. It should be unique per broker. */
        0,                                    /* The length of the client Id, filled in later as not const. */
        pdFALSE,                              /* Deprecated. */
        NULL,                                 /* User data supplied to the callback. Can be NULL. */
        NULL,                                 /* Callback used to report various events. Can be NULL. */
        NULL,                                 /* Certificate used for secure connection. Can be NULL. */
        0                                     /* Size of certificate used for secure connection. */
    };

    /* Check this function has not already been executed. */
    configASSERT( xMQTTHandle == NULL );

    /* The MQTT client object must be created before it can be used.  The
     * maximum number of MQTT client objects that can exist simultaneously
     * is set by mqttconfigMAX_BROKERS. */
    xReturned = MQTT_AGENT_Create( &xMQTTHandle );

    if( xReturned == eMQTTAgentSuccess )
    {
        /* Fill in the MQTTAgentConnectParams_t member that is not const,
         * and therefore could not be set in the initializer (where
         * xConnectParameters is declared in this function). */
        xConnectParameters.usClientIdLength = ( uint16_t ) strlen( ( const char * ) pubCLIENT_ID );

        /* Connect to the broker. */
        configPRINTF( ( "MQTT attempting to connect to %s.\r\n", clientcredentialMQTT_BROKER_ENDPOINT ) );
        xReturned = MQTT_AGENT_Connect( xMQTTHandle,
                                        &xConnectParameters,
                                        democonfigMQTT_PUB_TLS_NEGOTIATION_TIMEOUT );

        if( xReturned != eMQTTAgentSuccess )
        {
            /* Could not connect, so delete the MQTT client. */
            ( void ) MQTT_AGENT_Delete( xMQTTHandle );
            configPRINTF( ( "ERROR:  MQTT failed to connect with error %d.\r\n", xReturned ) );
        }
        else
        {
            configPRINTF( ( "MQTT connected.\r\n" ) );
            xReturn = pdPASS;
        }
    }

    return xReturn;
}
/*-----------------------------------------------------------*/

/**
  * @brief  fill the buffer with the sensor values
  * @param  none
  * @param Buffer is the char pointer for the buffer to be filled
  * @param Size size of the above buffer
  * @retval 0 in case of success
  *         -1 in case of failure
  */
int PrepareSensorsData(char * Buffer, int Size, char * deviceID)
{
    float    TEMPERATURE_Value;
    float    HUMIDITY_Value;
    float    PRESSURE_Value;
    int16_t  ACC_Value[3];
    float    GYR_Value[3];
    int16_t  MAG_Value[3];

    char * Buff = Buffer;
    int BuffSize = Size;
    int snprintfreturn = 0;

    TEMPERATURE_Value = BSP_TSENSOR_ReadTemp();
    HUMIDITY_Value = BSP_HSENSOR_ReadHumidity();
    PRESSURE_Value = BSP_PSENSOR_ReadPressure();
    BSP_ACCELERO_AccGetXYZ(ACC_Value);
    BSP_GYRO_GetXYZ(GYR_Value);
    BSP_MAGNETO_GetXYZ(MAG_Value);

   if (deviceID != NULL)
   {
       /* Format data for transmission to AWS */
       snprintfreturn = snprintf( Buff, BuffSize, "{\"Board_id\":\"%s\","
            "\"Temp\": %d, \"Hum\": %d, \"Press\": %d, "
            "\"Accel_X\": %d, \"Accel_Y\": %d, \"Accel_Z\": %d, "
            "\"Gyro_X\": %d, \"Gyro_Y\": %d, \"Gyro_Z\": %d, "
            "\"Magn_X\": %d, \"Magn_Y\": %d, \"Magn_Z\": %d"
            "}",
            deviceID,
            (int)TEMPERATURE_Value, (int)HUMIDITY_Value, (int)PRESSURE_Value,
            ACC_Value[0], ACC_Value[1], ACC_Value[2],
            (int)GYR_Value[0], (int)GYR_Value[1], (int)GYR_Value[2],
            MAG_Value[0], MAG_Value[1], MAG_Value[2] );
   }

    /* Check total size to be less than buffer size
    *  if the return is >=0 and <n, then
    *  the entire string was successfully formatted; if the return is
    *  >=n, the string was truncated (but there is still a null char
    *  at the end of what was written); if the return is <0, there was
    *  an error.
    */
  if (snprintfreturn >= 0 && snprintfreturn < Size)
  {
      return 0;
  }
  else if(snprintfreturn >= Size)
  {
      configPRINT_STRING("Data Pack truncated\n");
      return 0;
  }
  else
  {
      configPRINT_STRING("Data Pack Error\n");
      return -1;
  }
}
/*-----------------------------------------------------------*/

static void prvPublishNextMessage( void )
{
    MQTTAgentPublishParams_t xPublishParameters;
    MQTTAgentReturnCode_t xReturned;
    char cDataBuffer[ pubMAX_DATA_LENGTH ];

    /* Check this function is not being called before the MQTT client object has
     * been created. */
    configASSERT( xMQTTHandle != NULL );

    /* create desired message */
    if (PrepareSensorsData(cDataBuffer, sizeof(cDataBuffer), (char *) clientcredentialIOT_THING_NAME) != 0)
    {
    	configPRINTF( ( "Error obtaining sensor data\r\n" ) );
    	return;
    }

    /* Setup the publish parameters. */
    memset( &( xPublishParameters ), 0x00, sizeof( xPublishParameters ) );
    xPublishParameters.pucTopic = pubTOPIC_NAME;
    xPublishParameters.pvData = cDataBuffer;
    xPublishParameters.usTopicLength = ( uint16_t ) strlen( ( const char * ) pubTOPIC_NAME );
    xPublishParameters.ulDataLength = ( uint32_t ) strlen( cDataBuffer );
    xPublishParameters.xQoS = eMQTTQoS1;

    /* Publish the message. */
    xReturned = MQTT_AGENT_Publish( xMQTTHandle,
                                    &( xPublishParameters ),
                                    democonfigMQTT_TIMEOUT );

    if( xReturned == eMQTTAgentSuccess )
    {
        configPRINTF( ( "MQTT successfully published '%s'\r\n", cDataBuffer ) );
    }
    else
    {
        configPRINTF( ( "ERROR:  MQTT failed to publish '%s'\r\n", cDataBuffer ) );
    }

    /* Remove compiler warnings in case configPRINTF() is not defined. */
    ( void ) xReturned;
}
/*-----------------------------------------------------------*/

static void prvMQTTConnectAndPublishTask( void * pvParameters )
{
    BaseType_t xIterationCount;
    BaseType_t xReturned;

    const TickType_t xDelay = pdMS_TO_TICKS( democonfigSAMPLING_DELAY_SECONDS * 1000UL );
    const BaseType_t xIterationsInAMinute = 60 / 5;

    /* Avoid compiler warnings about unused parameters. */
    ( void ) pvParameters;

    /* Create the MQTT client object and connect it to the MQTT broker. */
    xReturned = prvCreateClientAndConnectToBroker();

    if( xReturned == pdPASS )
    {
        configPRINTF( ( "MQTT: successfully connected to broker.\r\n" ) );

        /* MQTT client is now connected to a broker.  Publish a message
         * every five seconds until a minute has elapsed. */
        for( xIterationCount = 0;
             ( democonfigITERATE_FOREVER || ( xIterationCount < xIterationsInAMinute ) );
             xIterationCount++ )
        {
            prvPublishNextMessage();

            /* Five seconds delay between publishes. */
            vTaskDelay( xDelay );
        }
    }
    else
    {
        configPRINTF( ( "MQTT: could not connect to broker.\r\n" ) );
    }

    /* Disconnect the client. */
    ( void ) MQTT_AGENT_Disconnect( xMQTTHandle, democonfigMQTT_TIMEOUT );

    /* End the demo by deleting all created resources. */
    configPRINTF( ( "Sensor demo finished.\r\n" ) );
    vTaskDelete( NULL ); /* Delete this task. */
}
/*-----------------------------------------------------------*/

void vRunSensorDemo( void )
{
    configPRINTF( ( "Creating MQTT Publishing Task...\r\n" ) );

    /* Create the task that publishes messages to the MQTT broker periodically. */
    ( void ) xTaskCreate( prvMQTTConnectAndPublishTask,        /* The function that implements the demo task. */
                          "SensorPub",                         /* The name to assign to the task being created. */
                          democonfigMQTT_PUB_TASK_STACK_SIZE, /* The size, in WORDS (not bytes), of the stack to allocate for the task being created. */
                          NULL,                                /* The task parameter is not being used. */
                          democonfigMQTT_PUB_TASK_PRIORITY,   /* The priority at which the task being created will run. */
                          NULL );                              /* Not storing the task's handle. */
}
/*-----------------------------------------------------------*/
