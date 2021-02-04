/*************************************************
 Copyright (c) 2020
 All rights reserved.
 File name:     mqtt_app.c
 Description:
 History:
 1. Version:
    Date:       2021-02-03
    Author:     WKJay
    Modify:
*************************************************/
#include "mqtt_app.h"
#include "mqtt_net_port.h"

/* Locals */
static int mStopRead = 0;
static volatile word16 mPacketIdLast;

/* Maximum size for network read/write callbacks. There is also a v5 define that
   describes the max MQTT control packet size, DEFAULT_MAX_PKT_SZ. */
#define MAX_BUFFER_SIZE 1024
#define TEST_MESSAGE    "test"

static const char* kDefTopicName = DEFAULT_TOPIC_NAME;
static const char* kDefClientId = DEFAULT_CLIENT_ID;

void mqtt_init_ctx(MQTTCtx* mqttCtx) {
    XMEMSET(mqttCtx, 0, sizeof(MQTTCtx));
    mqttCtx->host = DEFAULT_MQTT_HOST;
    mqttCtx->qos = DEFAULT_MQTT_QOS;
    mqttCtx->clean_session = 1;
    mqttCtx->keep_alive_sec = DEFAULT_KEEP_ALIVE_SEC;
    mqttCtx->client_id = kDefClientId;
    mqttCtx->topic_name = kDefTopicName;
    mqttCtx->cmd_timeout_ms = DEFAULT_CMD_TIMEOUT_MS;
#ifdef WOLFMQTT_V5
    mqttCtx->max_packet_size = DEFAULT_MAX_PKT_SZ;
    mqttCtx->topic_alias = 1;
    mqttCtx->topic_alias_max = 1;
#endif
}

word16 mqtt_get_packetid(void)
{
    /* Check rollover */
    if (mPacketIdLast >= MAX_PACKET_ID) {
        mPacketIdLast = 0;
    }

    return ++mPacketIdLast;
}

static int mqtt_message_cb(MqttClient* client, MqttMessage* msg, byte msg_new,
                           byte msg_done) {
    byte buf[PRINT_BUFFER_SIZE + 1];
    word32 len;
    MQTTCtx* mqttCtx = (MQTTCtx*)client->ctx;

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

        /* for test mode: check if TEST_MESSAGE was received */
        if (mqttCtx->test_mode) {
            if (XSTRLEN(TEST_MESSAGE) == msg->buffer_len &&
                XSTRNCMP(TEST_MESSAGE, (char*)msg->buffer, msg->buffer_len) ==
                    0) {
                mStopRead = 1;
            }
        }
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

#ifdef WOLFMQTT_DISCONNECT_CB
/* callback indicates a network error occurred */
static int mqtt_disconnect_cb(MqttClient* client, int error_code, void* ctx) {
    (void)client;
    (void)ctx;
    PRINTF("Network Error Callback: %s (error %d)",
           MqttClient_ReturnCodeToString(error_code), error_code);
    return 0;
}
#endif

#ifdef WOLFMQTT_PROPERTY_CB
/* The property callback is called after decoding a packet that contains at
   least one property. The property list is deallocated after returning from
   the callback. */
static int mqtt_property_cb(MqttClient* client, MqttProp* head, void* ctx) {
    MqttProp* prop = head;
    int rc = 0;
    MQTTCtx* mqttCtx;

    if ((client == NULL) || (client->ctx == NULL)) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    mqttCtx = (MQTTCtx*)client->ctx;

    while (prop != NULL) {
        switch (prop->type) {
            case MQTT_PROP_ASSIGNED_CLIENT_ID:
                /* Store client ID in global */
                mqttCtx->client_id = &gClientId[0];

                /* Store assigned client ID from CONNACK*/
                XSTRNCPY((char*)mqttCtx->client_id, prop->data_str.str,
                         MAX_CLIENT_ID_LEN - 1);
                ((char*)mqttCtx->client_id)[MAX_CLIENT_ID_LEN - 1] =
                    0; /* really want strlcpy() semantics, but that's
                          non-portable. */
                break;

            case MQTT_PROP_SUBSCRIPTION_ID_AVAIL:
                mqttCtx->subId_not_avail = prop->data_byte == 0;
                break;

            case MQTT_PROP_TOPIC_ALIAS_MAX:
                mqttCtx->topic_alias_max =
                    (mqttCtx->topic_alias_max < prop->data_short)
                        ? mqttCtx->topic_alias_max
                        : prop->data_short;
                break;

            case MQTT_PROP_MAX_PACKET_SZ:
                if ((prop->data_int > 0) &&
                    (prop->data_int <= MQTT_PACKET_SZ_MAX)) {
                    client->packet_sz_max =
                        (client->packet_sz_max < prop->data_int)
                            ? client->packet_sz_max
                            : prop->data_int;
                } else {
                    /* Protocol error */
                    rc = MQTT_CODE_ERROR_PROPERTY;
                }
                break;

            case MQTT_PROP_SERVER_KEEP_ALIVE:
                mqttCtx->keep_alive_sec = prop->data_short;
                break;

            case MQTT_PROP_MAX_QOS:
                client->max_qos = prop->data_byte;
                break;

            case MQTT_PROP_RETAIN_AVAIL:
                client->retain_avail = prop->data_byte;
                break;

            case MQTT_PROP_REASON_STR:
                PRINTF("Reason String: %s", prop->data_str.str);
                break;

            case MQTT_PROP_PAYLOAD_FORMAT_IND:
            case MQTT_PROP_MSG_EXPIRY_INTERVAL:
            case MQTT_PROP_CONTENT_TYPE:
            case MQTT_PROP_RESP_TOPIC:
            case MQTT_PROP_CORRELATION_DATA:
            case MQTT_PROP_SUBSCRIPTION_ID:
            case MQTT_PROP_SESSION_EXPIRY_INTERVAL:
            case MQTT_PROP_TOPIC_ALIAS:
            case MQTT_PROP_TYPE_MAX:
            case MQTT_PROP_RECEIVE_MAX:
            case MQTT_PROP_USER_PROP:
            case MQTT_PROP_WILDCARD_SUB_AVAIL:
            case MQTT_PROP_SHARED_SUBSCRIPTION_AVAIL:
            case MQTT_PROP_RESP_INFO:
            case MQTT_PROP_SERVER_REF:
            case MQTT_PROP_AUTH_METHOD:
            case MQTT_PROP_AUTH_DATA:
            case MQTT_PROP_NONE:
                break;
            case MQTT_PROP_REQ_PROB_INFO:
            case MQTT_PROP_WILL_DELAY_INTERVAL:
            case MQTT_PROP_REQ_RESP_INFO:
            default:
                /* Invalid */
                rc = MQTT_CODE_ERROR_PROPERTY;
                break;
        }
        prop = prop->next;
    }

    (void)ctx;

    return rc;
}
#endif

void mqtt_free_ctx(MQTTCtx* mqttCtx)
{
    if (mqttCtx == NULL) {
        return;
    }

    if (mqttCtx->dynamicTopic && mqttCtx->topic_name) {
        WOLFMQTT_FREE((char*)mqttCtx->topic_name);
        mqttCtx->topic_name = NULL;
    }
    if (mqttCtx->dynamicClientId && mqttCtx->client_id) {
        WOLFMQTT_FREE((char*)mqttCtx->client_id);
        mqttCtx->client_id = NULL;
    }
}

int mqtt_test(void) {
    int rc = MQTT_CODE_SUCCESS, i;
    MQTTCtx mqttCtx;
    MQTTCtx* mqtt_ctx = &mqttCtx;
    mqtt_init_ctx(mqtt_ctx);

    // connect to broker over network
    rc = MqttClientNet_Init(&mqtt_ctx->net, mqtt_ctx);
    PRINTF("MQTT Net Init: %s (%d)", MqttClient_ReturnCodeToString(rc), rc);
    if (rc != MQTT_CODE_SUCCESS) {
        goto exit;
    }

    /* setup tx/rx buffers */
    mqtt_ctx->tx_buf = (byte*)WOLFMQTT_MALLOC(MAX_BUFFER_SIZE);
    mqtt_ctx->rx_buf = (byte*)WOLFMQTT_MALLOC(MAX_BUFFER_SIZE);

    /* Initialize MqttClient structure */
    rc = MqttClient_Init(&mqtt_ctx->client, &mqtt_ctx->net, mqtt_message_cb,
                         mqtt_ctx->tx_buf, MAX_BUFFER_SIZE, mqtt_ctx->rx_buf,
                         MAX_BUFFER_SIZE, mqtt_ctx->cmd_timeout_ms);
    PRINTF("MQTT Init: %s (%d)", MqttClient_ReturnCodeToString(rc), rc);
    if (rc != MQTT_CODE_SUCCESS) {
        goto exit;
    }

    /* The client.ctx will be stored in the cert callback ctx during
       MqttSocket_Connect for use by mqtt_tls_verify_cb */
    mqtt_ctx->client.ctx = mqtt_ctx;

#ifdef WOLFMQTT_DISCONNECT_CB
    /* setup disconnect callback */
    rc = MqttClient_SetDisconnectCallback(&mqtt_ctx->client, mqtt_disconnect_cb,
                                          NULL);
    if (rc != MQTT_CODE_SUCCESS) {
        goto exit;
    }
#endif
#ifdef WOLFMQTT_PROPERTY_CB
    rc = MqttClient_SetPropertyCallback(&mqtt_ctx->client, mqtt_property_cb,
                                        NULL);
    if (rc != MQTT_CODE_SUCCESS) {
        goto exit;
    }
#endif

    /* Connect to broker */
    rc =
        MqttClient_NetConnect(&mqtt_ctx->client, mqtt_ctx->host, mqtt_ctx->port,
                              DEFAULT_CON_TIMEOUT_MS, mqtt_ctx->use_tls, NULL);

    PRINTF("MQTT Socket Connect: %s (%d)", MqttClient_ReturnCodeToString(rc),
           rc);
    if (rc != MQTT_CODE_SUCCESS) {
        goto exit;
    }

    /* Build connect packet */
    XMEMSET(&mqtt_ctx->connect, 0, sizeof(MqttConnect));
    mqtt_ctx->connect.keep_alive_sec = mqtt_ctx->keep_alive_sec;
    mqtt_ctx->connect.clean_session = mqtt_ctx->clean_session;
    mqtt_ctx->connect.client_id = mqtt_ctx->client_id;

    /* Last will and testament sent by broker to subscribers
        of topic when broker connection is lost */
    XMEMSET(&mqtt_ctx->lwt_msg, 0, sizeof(mqtt_ctx->lwt_msg));
    mqtt_ctx->connect.lwt_msg = &mqtt_ctx->lwt_msg;
    mqtt_ctx->connect.enable_lwt = mqtt_ctx->enable_lwt;
    if (mqtt_ctx->enable_lwt) {
        /* Send client id in LWT payload */
        mqtt_ctx->lwt_msg.qos = mqtt_ctx->qos;
        mqtt_ctx->lwt_msg.retain = 0;
        mqtt_ctx->lwt_msg.topic_name = WOLFMQTT_TOPIC_NAME "lwttopic";
        mqtt_ctx->lwt_msg.buffer = (byte*)mqtt_ctx->client_id;
        mqtt_ctx->lwt_msg.total_len = (word16)XSTRLEN(mqtt_ctx->client_id);
    }
    /* Optional authentication */
    mqtt_ctx->connect.username = mqtt_ctx->username;
    mqtt_ctx->connect.password = mqtt_ctx->password;

    /* Send Connect and wait for Connect Ack */
    rc = MqttClient_Connect(&mqtt_ctx->client, &mqtt_ctx->connect);
    PRINTF("MQTT Connect: Proto (%s), %s (%d)",
           MqttClient_GetProtocolVersionString(&mqtt_ctx->client),
           MqttClient_ReturnCodeToString(rc), rc);
    if (rc != MQTT_CODE_SUCCESS) {
        goto disconn;
    }

    /* Validate Connect Ack info */
    PRINTF("MQTT Connect Ack: Return Code %u, Session Present %d",
           mqtt_ctx->connect.ack.return_code,
           (mqtt_ctx->connect.ack.flags & MQTT_CONNECT_ACK_FLAG_SESSION_PRESENT)
               ? 1
               : 0);

    /* Build list of topics */
    XMEMSET(&mqtt_ctx->subscribe, 0, sizeof(MqttSubscribe));

    i = 0;
    mqtt_ctx->topics[i].topic_filter = mqtt_ctx->topic_name;
    mqtt_ctx->topics[i].qos = mqtt_ctx->qos;

    /* Subscribe Topic */
    mqtt_ctx->subscribe.packet_id = mqtt_get_packetid();
    mqtt_ctx->subscribe.topic_count =
        sizeof(mqtt_ctx->topics) / sizeof(MqttTopic);
    mqtt_ctx->subscribe.topics = mqtt_ctx->topics;

    rc = MqttClient_Subscribe(&mqtt_ctx->client, &mqtt_ctx->subscribe);

    PRINTF("MQTT Subscribe: %s (%d)", MqttClient_ReturnCodeToString(rc), rc);
    if (rc != MQTT_CODE_SUCCESS) {
        goto disconn;
    }

    /* show subscribe results */
    for (i = 0; i < mqtt_ctx->subscribe.topic_count; i++) {
        MqttTopic* topic = &mqtt_ctx->subscribe.topics[i];
        PRINTF("  Topic %s, Qos %u, Return Code %u", topic->topic_filter,
               topic->qos, topic->return_code);
    }

    /* Publish Topic */
    XMEMSET(&mqtt_ctx->publish, 0, sizeof(MqttPublish));
    mqtt_ctx->publish.retain = 0;
    mqtt_ctx->publish.qos = mqtt_ctx->qos;
    mqtt_ctx->publish.duplicate = 0;
    mqtt_ctx->publish.topic_name = mqtt_ctx->topic_name;
    mqtt_ctx->publish.packet_id = mqtt_get_packetid();
    mqtt_ctx->publish.buffer = (byte*)TEST_MESSAGE;
    mqtt_ctx->publish.total_len = (word16)XSTRLEN(TEST_MESSAGE);

    rc = MqttClient_Publish(&mqtt_ctx->client, &mqtt_ctx->publish);
    PRINTF("MQTT Publish: Topic %s, %s (%d)", mqtt_ctx->publish.topic_name,
           MqttClient_ReturnCodeToString(rc), rc);
    if (rc != MQTT_CODE_SUCCESS) {
        goto disconn;
    }

    /* Read Loop */
    PRINTF("MQTT Waiting for message...");

    do {
        /* Try and read packet */
        rc =
            MqttClient_WaitMessage(&mqtt_ctx->client, mqtt_ctx->cmd_timeout_ms);

#ifdef WOLFMQTT_NONBLOCK
        /* Track elapsed time with no activity and trigger timeout */
        rc = mqtt_check_timeout(rc, &mqtt_ctx->start_sec,
                                mqtt_ctx->cmd_timeout_ms / 1000);
#endif

        /* check for test mode */
        if (mStopRead) {
            rc = MQTT_CODE_SUCCESS;
            PRINTF("MQTT Exiting...");
            break;
        }
        /* check return code */
#ifdef WOLFMQTT_ENABLE_STDIN_CAP
        else if (rc == MQTT_CODE_STDIN_WAKE) {
            XMEMSET(mqtt_ctx->rx_buf, 0, MAX_BUFFER_SIZE);
            if (XFGETS((char*)mqtt_ctx->rx_buf, MAX_BUFFER_SIZE - 1, stdin) !=
                NULL) {
                rc = (int)XSTRLEN((char*)mqtt_ctx->rx_buf);

                /* Publish Topic */
                mqtt_ctx->stat = WMQ_PUB;
                XMEMSET(&mqtt_ctx->publish, 0, sizeof(MqttPublish));
                mqtt_ctx->publish.retain = 0;
                mqtt_ctx->publish.qos = mqtt_ctx->qos;
                mqtt_ctx->publish.duplicate = 0;
                mqtt_ctx->publish.topic_name = mqtt_ctx->topic_name;
                mqtt_ctx->publish.packet_id = mqtt_get_packetid();
                mqtt_ctx->publish.buffer = mqtt_ctx->rx_buf;
                mqtt_ctx->publish.total_len = (word16)rc;
                rc = MqttClient_Publish(&mqtt_ctx->client, &mqtt_ctx->publish);
                PRINTF("MQTT Publish: Topic %s, %s (%d)",
                       mqtt_ctx->publish.topic_name,
                       MqttClient_ReturnCodeToString(rc), rc);
            }
        }
#endif
        else if (rc == MQTT_CODE_ERROR_TIMEOUT) {
            /* Keep Alive */
            PRINTF("Keep-alive timeout, sending ping");

            rc = MqttClient_Ping(&mqtt_ctx->client);
            if (rc != MQTT_CODE_SUCCESS) {
                PRINTF("MQTT Ping Keep Alive Error: %s (%d)",
                       MqttClient_ReturnCodeToString(rc), rc);
                break;
            }
        } else if (rc != MQTT_CODE_SUCCESS) {
            /* There was an error */
            PRINTF("MQTT Message Wait: %s (%d)",
                   MqttClient_ReturnCodeToString(rc), rc);
            break;
        }
    } while (1);

    /* Check for error */
    if (rc != MQTT_CODE_SUCCESS) {
        goto disconn;
    }

    /* Unsubscribe Topics */
    XMEMSET(&mqtt_ctx->unsubscribe, 0, sizeof(MqttUnsubscribe));
    mqtt_ctx->unsubscribe.packet_id = mqtt_get_packetid();
    mqtt_ctx->unsubscribe.topic_count =
        sizeof(mqtt_ctx->topics) / sizeof(MqttTopic);
    mqtt_ctx->unsubscribe.topics = mqtt_ctx->topics;

    /* Unsubscribe Topics */
    rc = MqttClient_Unsubscribe(&mqtt_ctx->client, &mqtt_ctx->unsubscribe);

    PRINTF("MQTT Unsubscribe: %s (%d)", MqttClient_ReturnCodeToString(rc), rc);
    if (rc != MQTT_CODE_SUCCESS) {
        goto disconn;
    }
    mqtt_ctx->return_code = rc;
disconn:
    /* Disconnect */
    rc = MqttClient_Disconnect_ex(&mqtt_ctx->client, &mqtt_ctx->disconnect);

    PRINTF("MQTT Disconnect: %s (%d)", MqttClient_ReturnCodeToString(rc), rc);
    if (rc != MQTT_CODE_SUCCESS) {
        goto disconn;
    }

    rc = MqttClient_NetDisconnect(&mqtt_ctx->client);

    PRINTF("MQTT Socket Disconnect: %s (%d)", MqttClient_ReturnCodeToString(rc),
           rc);

exit:
    /* Free resources */
    if (mqtt_ctx->tx_buf) WOLFMQTT_FREE(mqtt_ctx->tx_buf);
    if (mqtt_ctx->rx_buf) WOLFMQTT_FREE(mqtt_ctx->rx_buf);

    /* Cleanup network */
    MqttClientNet_DeInit(&mqtt_ctx->net);

    MqttClient_DeInit(&mqtt_ctx->client);

    mqtt_free_ctx(&mqttCtx);

    return rc;
}
