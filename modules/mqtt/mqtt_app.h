#ifndef __MQTT_APP_H
#define __MQTT_APP_H

#define DEFAULT_HOST "broker.hivemq.com"
#define DEFAULT_CMD_TIMEOUT_MS  30000
#define DEFAULT_CON_TIMEOUT_MS  5000
#define DEFAULT_MQTT_QOS        MQTT_QOS_0
#define DEFAULT_KEEP_ALIVE_SEC  60
#define DEFAULT_CLIENT_ID       "WolfMQTTClient"
#define WOLFMQTT_TOPIC_NAME     "wolfMQTT/test/"
#define DEFAULT_TOPIC_NAME      WOLFMQTT_TOPIC_NAME"testTopic"
#define DEFAULT_AUTH_METHOD    "EXTERNAL"
#define PRINT_BUFFER_SIZE       80

#ifdef WOLFMQTT_V5
#define DEFAULT_MAX_PKT_SZ      768 /* The max MQTT control packet size the
                                       client is willing to accept. */
#endif

int mqtt_init(void);
#endif /* __MQTT_APP_H */
