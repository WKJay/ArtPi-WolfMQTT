/*************************************************
 Copyright (c) 2020
 All rights reserved.
 File name:     mqtt_app.c
 Description:
 History:
 1. Version:
    Date:       2021-02-11
    Author:     WKJay
    Modify:
*************************************************/
#include <rtthread.h>
#include <wolfmqtt.h>
#include <mqtt_app.h>

MQTTCtx_t* mqtt_ctx;

static int mqtt_message_cb(MqttClient* client, MqttMessage* msg, byte msg_new,
                           byte msg_done) {
    byte buf[PRINT_BUFFER_SIZE + 1];
    word32 len;
    MQTTCtx_t* mqttCtx = (MQTTCtx_t*)client->ctx;

    (void)mqttCtx;

    if (msg_new) {
        /* Determine min size to dump */
        len = msg->topic_name_len;
        if (len > PRINT_BUFFER_SIZE) {
            len = PRINT_BUFFER_SIZE;
        }
        XMEMCPY(buf, msg->topic_name, len);
        buf[len] = '\0'; /* Make sure its null terminated */

        /* Print incoming message */
        PRINTF("MQTT Message: Topic %s, Qos %d, Len %u", buf, msg->qos,
               msg->total_len);
    }

    /* Print message payload */
    len = msg->buffer_len;
    if (len > PRINT_BUFFER_SIZE) {
        len = PRINT_BUFFER_SIZE;
    }
    XMEMCPY(buf, msg->buffer, len);
    buf[len] = '\0'; /* Make sure its null terminated */
    PRINTF("Payload (%d - %d): %s", msg->buffer_pos, msg->buffer_pos + len,
           buf);

    if (msg_done) {
        PRINTF("MQTT Message: Done");
    }

    /* Return negative to terminate publish processing */
    return MQTT_CODE_SUCCESS;
}

int mqtt_init(void) {
    mqtt_ctx = mqtt_ctx_create(
        DEFAULT_HOST, DEFAULT_CLIENT_ID, DEFAULT_TOPIC_NAME, DEFAULT_MQTT_QOS,
        DEFAULT_KEEP_ALIVE_SEC, DEFAULT_CMD_TIMEOUT_MS, mqtt_message_cb);
    if (mqtt_ctx == NULL) {
        printf("create mqtt context failed.\r\n");
        return -1;
    }
    if (mqtt_ctx_connect(mqtt_ctx, DEFAULT_CON_TIMEOUT_MS) < 0) {
        mqtt_ctx_free(mqtt_ctx);
        return -1;
    }
    if (mqtt_subscribe(mqtt_ctx, WOLFMQTT_TOPIC_NAME, DEFAULT_MQTT_QOS) < 0) {
        mqtt_close(mqtt_ctx);
        return -1;
    }
    mqtt_start(mqtt_ctx);
    return 0;
}

int mqtt_publish_test(void) {
    const char* TEST_STR = "this is a test";
    int str_len = strlen(TEST_STR);
    mqtt_publish(mqtt_ctx, TEST_STR, str_len, "/wolfMQTT/test/");
}
MSH_CMD_EXPORT(mqtt_publish_test, mqtt_publish_test);
