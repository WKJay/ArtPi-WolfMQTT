/* mqttnet.c
 *
 * Copyright (C) 2006-2020 wolfSSL Inc.
 *
 * This file is part of wolfMQTT.
 *
 * wolfMQTT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfMQTT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

/* Include the autoconf generated config.h */
#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_client.h"
#include "examples/mqttnet.h"
#include "examples/mqttexample.h"

/* FreeRTOS TCP */
#ifdef FREERTOS_TCP
    #include "FreeRTOS.h"
    #include "task.h"
    #include "FreeRTOS_IP.h"
    #include "FreeRTOS_DNS.h"
    #include "FreeRTOS_Sockets.h"

    #define SOCKET_T                     Socket_t
    #define SOCK_ADDR_IN                 struct freertos_sockaddr

/* FreeRTOS and LWIP */
#elif defined(FREERTOS) && defined(WOLFSSL_LWIP)
    /* Scheduler includes. */
    #include "FreeRTOS.h"
    #include "task.h"
    #include "semphr.h"

    /* lwIP includes. */
    #include "lwip/api.h"
    #include "lwip/tcpip.h"
    #include "lwip/memp.h"
    #include "lwip/stats.h"
    #include "lwip/sockets.h"
    #include "lwip/netdb.h"
#endif

/* Setup defaults */
#ifndef SOCK_OPEN
    #define SOCK_OPEN       socket
#endif
#ifndef SOCKET_T
    #define SOCKET_T        int
#endif
#ifndef SOERROR_T
    #define SOERROR_T       int
#endif
#ifndef SELECT_FD
    #define SELECT_FD(fd)   ((fd) + 1)
#endif
#ifndef SOCKET_INVALID
    #define SOCKET_INVALID  ((SOCKET_T)0)
#endif
#ifndef SOCK_CONNECT
    #define SOCK_CONNECT    connect
#endif
#ifndef SOCK_SEND
    #define SOCK_SEND(s,b,l,f) send((s), (b), (size_t)(l), (f))
#endif
#ifndef SOCK_RECV
    #define SOCK_RECV(s,b,l,f) recv((s), (b), (size_t)(l), (f))
#endif
#ifndef SOCK_CLOSE
    #define SOCK_CLOSE      close
#endif
#ifndef SOCK_ADDR_IN
    #define SOCK_ADDR_IN    struct sockaddr_in
#endif
#ifdef SOCK_ADDRINFO
    #define SOCK_ADDRINFO   struct addrinfo
#endif


/* Local context for Net callbacks */
typedef enum {
    SOCK_BEGIN = 0,
    SOCK_CONN,
} NB_Stat;


#if 0 /* TODO: add multicast support */
typedef struct MulticastContext {

} MulticastContext;
#endif


typedef struct _SocketContext {
    SOCKET_T fd;
    NB_Stat stat;
    SOCK_ADDR_IN addr;
#ifdef MICROCHIP_MPLAB_HARMONY
    word32 bytes;
#endif
#if defined(WOLFMQTT_MULTITHREAD) && defined(WOLFMQTT_ENABLE_STDIN_CAP)
    /* "self pipe" -> signal wake sleep() */
    SOCKET_T pfd[2];
#endif
    MQTTCtx* mqttCtx;
} SocketContext;

/* Private functions */

/* -------------------------------------------------------------------------- */
/* FREERTOS TCP NETWORK CALLBACK EXAMPLE */
/* -------------------------------------------------------------------------- */
#ifdef FREERTOS_TCP

#ifndef WOLFMQTT_NO_TIMEOUT
    static SocketSet_t gxFDSet = NULL;
#endif
static int NetConnect(void *context, const char* host, word16 port,
    int timeout_ms)
{
    SocketContext *sock = (SocketContext*)context;
    uint32_t hostIp = 0;
    int rc = -1;
    MQTTCtx* mqttCtx = sock->mqttCtx;

    switch (sock->stat) {
    case SOCK_BEGIN:
        PRINTF("NetConnect: Host %s, Port %u, Timeout %d ms, Use TLS %d",
            host, port, timeout_ms, mqttCtx->use_tls);

        hostIp = FreeRTOS_gethostbyname_a(host, NULL, 0, 0);
        if (hostIp == 0)
            break;

        sock->addr.sin_family = FREERTOS_AF_INET;
        sock->addr.sin_port = FreeRTOS_htons(port);
        sock->addr.sin_addr = hostIp;

        /* Create socket */
        sock->fd = FreeRTOS_socket(sock->addr.sin_family, FREERTOS_SOCK_STREAM,
                                   FREERTOS_IPPROTO_TCP);

        if (sock->fd == FREERTOS_INVALID_SOCKET)
            break;

#ifndef WOLFMQTT_NO_TIMEOUT
        /* Set timeouts for socket */
        timeout_ms = pdMS_TO_TICKS(timeout_ms);
        FreeRTOS_setsockopt(sock->fd, 0, FREERTOS_SO_SNDTIMEO,
            (void*)&timeout_ms, sizeof(timeout_ms));
        FreeRTOS_setsockopt(sock->fd, 0, FREERTOS_SO_RCVTIMEO,
            (void*)&timeout_ms, sizeof(timeout_ms));
#else
        (void)timeout_ms;
#endif
        sock->stat = SOCK_CONN;

        FALL_THROUGH;
    case SOCK_CONN:
        /* Start connect */
        rc = FreeRTOS_connect(sock->fd, (SOCK_ADDR_IN*)&sock->addr,
                              sizeof(sock->addr));
        break;
    }

    return rc;
}

static int NetRead(void *context, byte* buf, int buf_len,
    int timeout_ms)
{
    SocketContext *sock = (SocketContext*)context;
    int rc = -1, timeout = 0;
    word32 bytes = 0;

    if (context == NULL || buf == NULL || buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

#ifndef WOLFMQTT_NO_TIMEOUT
    /* Create the set of sockets that will be passed into FreeRTOS_select(). */
    if (gxFDSet == NULL)
        gxFDSet = FreeRTOS_CreateSocketSet();
    if (gxFDSet == NULL)
        return MQTT_CODE_ERROR_OUT_OF_BUFFER;
    timeout_ms = pdMS_TO_TICKS(timeout_ms); /* convert ms to ticks */
#else
    (void)timeout_ms;
#endif

    /* Loop until buf_len has been read, error or timeout */
    while ((bytes < buf_len) && (timeout == 0)) {

#ifndef WOLFMQTT_NO_TIMEOUT
        /* set the socket to do used */
        FreeRTOS_FD_SET(sock->fd, gxFDSet, eSELECT_READ | eSELECT_EXCEPT);

        /* Wait for any event within the socket set. */
        rc = FreeRTOS_select(gxFDSet, timeout_ms);
        if (rc != 0) {
            if (FreeRTOS_FD_ISSET(sock->fd, gxFDSet))
#endif
            {
                /* Try and read number of buf_len provided,
                    minus what's already been read */
                rc = (int)FreeRTOS_recv(sock->fd, &buf[bytes],
                    buf_len - bytes, 0);

                if (rc <= 0) {
                    break; /* Error */
                }
                else {
                    bytes += rc; /* Data */
                }
            }
#ifndef WOLFMQTT_NO_TIMEOUT
        }
        else {
            timeout = 1;
        }
#endif
    }

    if (rc == 0 || timeout) {
        rc = MQTT_CODE_ERROR_TIMEOUT;
    }
    else if (rc < 0) {
    #ifdef WOLFMQTT_NONBLOCK
        if (rc == -pdFREERTOS_ERRNO_EWOULDBLOCK) {
            return MQTT_CODE_CONTINUE;
        }
    #endif
        PRINTF("NetRead: Error %d", rc);
        rc = MQTT_CODE_ERROR_NETWORK;
    }
    else {
        rc = bytes;
    }

    return rc;
}

static int NetWrite(void *context, const byte* buf, int buf_len, int timeout_ms)
{
    SocketContext *sock = (SocketContext*)context;
    int rc = -1;

    (void)timeout_ms;

    if (context == NULL || buf == NULL || buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    rc = (int)FreeRTOS_send(sock->fd, buf, buf_len, 0);

    if (rc < 0) {
    #ifdef WOLFMQTT_NONBLOCK
        if (rc == -pdFREERTOS_ERRNO_EWOULDBLOCK) {
            return MQTT_CODE_CONTINUE;
        }
    #endif
        PRINTF("NetWrite: Error %d", rc);
        rc = MQTT_CODE_ERROR_NETWORK;
    }

    return rc;
}

static int NetDisconnect(void *context)
{
    SocketContext *sock = (SocketContext*)context;
    if (sock) {
        FreeRTOS_closesocket(sock->fd);
        sock->stat = SOCK_BEGIN;
    }

#ifndef WOLFMQTT_NO_TIMEOUT
    if (gxFDSet != NULL) {
        FreeRTOS_DeleteSocketSet(gxFDSet);
        gxFDSet = NULL;
    }
#endif

    return 0;
}


/* -------------------------------------------------------------------------- */
/* MICROCHIP HARMONY TCP NETWORK CALLBACK EXAMPLE */
/* -------------------------------------------------------------------------- */
#endif


/* Public Functions */
int MqttClientNet_Init(MqttNet* net, MQTTCtx* mqttCtx)
{
#if defined(USE_WINDOWS_API) && !defined(FREERTOS_TCP)
    WSADATA wsd;
    WSAStartup(0x0002, &wsd);
#endif

    if (net) {
        SocketContext* sockCtx;

        XMEMSET(net, 0, sizeof(MqttNet));
        net->connect = NetConnect;
        net->read = NetRead;
        net->write = NetWrite;
        net->disconnect = NetDisconnect;

        sockCtx = (SocketContext*)WOLFMQTT_MALLOC(sizeof(SocketContext));
        if (sockCtx == NULL) {
            return MQTT_CODE_ERROR_MEMORY;
        }
        net->context = sockCtx;
        XMEMSET(sockCtx, 0, sizeof(SocketContext));
        sockCtx->fd = SOCKET_INVALID;
        sockCtx->stat = SOCK_BEGIN;
        sockCtx->mqttCtx = mqttCtx;
    
    #if defined(WOLFMQTT_MULTITHREAD) && defined(WOLFMQTT_ENABLE_STDIN_CAP)
        /* setup the pipe for waking select() */
        if (pipe(sockCtx->pfd) != 0) {
            PRINTF("Failed to set up pipe for stdin");
            return -1;
        }
    #endif
    }

    return MQTT_CODE_SUCCESS;
}

#ifdef WOLFMQTT_SN
int SN_ClientNet_Init(MqttNet* net, MQTTCtx* mqttCtx)
{
    if (net) {
        SocketContext* sockCtx;

        XMEMSET(net, 0, sizeof(MqttNet));
        net->connect = SN_NetConnect;
        net->read = NetRead;
        net->write = NetWrite;
        net->peek = NetPeek;
        net->disconnect = NetDisconnect;

        sockCtx = (SocketContext*)WOLFMQTT_MALLOC(sizeof(SocketContext));
        if (sockCtx == NULL) {
            return MQTT_CODE_ERROR_MEMORY;
        }
        net->context = sockCtx;
        XMEMSET(sockCtx, 0, sizeof(SocketContext));
        sockCtx->stat = SOCK_BEGIN;
        sockCtx->mqttCtx = mqttCtx;

    #if 0 /* TODO: add multicast support */
        MulticastContext* multi_ctx;
        multi_ctx = (MulticastContext*)WOLFMQTT_MALLOC(sizeof(MulticastContext));
        if (multi_ctx == NULL) {
            return MQTT_CODE_ERROR_MEMORY;
        }
        net->multi_ctx = multi_ctx;
        XMEMSET(multi_ctx, 0, sizeof(MulticastContext));
        multi_ctx->stat = SOCK_BEGIN;
    #endif

    #if defined(WOLFMQTT_MULTITHREAD) && defined(WOLFMQTT_ENABLE_STDIN_CAP)
        /* setup the pipe for waking select() */
        if (pipe(sockCtx->pfd) != 0) {
            PRINTF("Failed to set up pipe for stdin");
            return -1;
        }
    #endif
    }

    return MQTT_CODE_SUCCESS;
}
#endif

int MqttClientNet_DeInit(MqttNet* net)
{
    if (net) {
        if (net->context) {
            WOLFMQTT_FREE(net->context);
        }
        XMEMSET(net, 0, sizeof(MqttNet));
    }
    return 0;
}

int MqttClientNet_Wake(MqttNet* net)
{
#if defined(WOLFMQTT_MULTITHREAD) && defined(WOLFMQTT_ENABLE_STDIN_CAP)
    if (net) {
        SocketContext* sockCtx = (SocketContext*)net->context;
        if (sockCtx) {
            /* wake the select() */
            if (write(sockCtx->pfd[1], "\n", 1) < 0) {
                PRINTF("Failed to wake select");
                return -1;
            }
        }
    }
#else
    (void)net;
#endif
    return 0;
}
