/*
 * Copyright (c) 2014-2016 Alibaba Group. All rights reserved.
 * License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "iot_import.h"
#include "CoAPExport.h"
#include "iot_import_coap.h"
#include "iot_import_dtls.h"
#include "CoAPNetwork.h"

#ifdef COAP_DTLS_SUPPORT
static void CoAPNetworkDTLS_freeSession (void *p_session);

unsigned int CoAPNetworkDTLS_read(void *p_session,
                                      unsigned char              *p_data,
                                      unsigned int               *p_datalen,
                                      unsigned int                timeout)
{
    unsigned int           err_code  = DTLS_SUCCESS;
    const unsigned int     read_len  = *p_datalen;
    DTLSContext           *context   = NULL;

    COAP_TRC("<< secure_datagram_read, read buffer len %d, timeout %d\r\n", read_len, timeout);
    if (NULL != p_session)
    {
        /* read dtls application data*/
        context = (DTLSContext *)p_session;
        err_code = HAL_DTLSSession_read(context, p_data, p_datalen, timeout);
        if(DTLS_PEER_CLOSE_NOTIFY == err_code
                || DTLS_FATAL_ALERT_MESSAGE  == err_code) {
            COAP_INFO("dtls session read failed return (0x%04x)\r\n", err_code);
            CoAPNetworkDTLS_freeSession(context);
        }
        if(DTLS_SUCCESS == err_code){
            return COAP_SUCCESS;
        }
        else{
            return COAP_ERROR_READ_FAILED;
        }
    }

    return COAP_ERROR_INVALID_PARAM;
}

unsigned int CoAPNetworkDTLS_write(void *p_session,
                                    const unsigned char        *p_data,
                                    unsigned int               *p_datalen)
{
    unsigned int err_code = DTLS_SUCCESS;
    if(NULL != p_session){
        err_code =  HAL_DTLSSession_write((DTLSContext *)p_session, p_data, p_datalen);
        if(DTLS_SUCCESS == err_code){
            return COAP_SUCCESS;
        }
        else{
            return COAP_ERROR_WRITE_FAILED;
        }
    }
    return COAP_ERROR_INVALID_PARAM;
}

static  void CoAPNetworkDTLS_freeSession (void *p_session)
{
    /* Free the session.*/
    HAL_DTLSSession_free((DTLSContext *)p_session);
}

void *CoAPNetworkDTLS_createSession(char *p_host,
                                        unsigned short         port,
                                        unsigned char         *p_ca_cert_pem)
{
    DTLSContext *context = NULL;
    coap_dtls_options_t dtls_options;

    memset(&dtls_options, 0x00, sizeof(coap_dtls_options_t));
    dtls_options.p_ca_cert_pem     = p_ca_cert_pem;
    dtls_options.p_host            = p_host;
    dtls_options.port              = port;

    context = HAL_DTLSSession_create(&dtls_options);
    return  (void *)context;
}

#endif

unsigned int CoAPNetwork_write(coap_network_t *p_network,
                                  const unsigned char  * p_data,
                                  unsigned int           datalen)
{
    int rc = COAP_ERROR_WRITE_FAILED;

#ifdef COAP_DTLS_SUPPORT
    if(COAP_ENDPOINT_DTLS == p_network->ep_type){
        rc = CoAPNetworkDTLS_write(p_network->context, p_data, &datalen);
    }
    else{
#endif
        rc = HAL_UDP_write((void *)p_network->context, p_data, datalen);
        COAP_DEBUG("[CoAP-NWK]: Network write return %d\r\n", rc);

        if(-1 == rc) {
            rc = COAP_ERROR_WRITE_FAILED;
        } else {
            rc = COAP_SUCCESS;
        }
#ifdef COAP_DTLS_SUPPORT
    }
#endif
    return (unsigned int)rc;
}

int CoAPNetwork_read(coap_network_t *network, unsigned char  *data,
                        unsigned int datalen, unsigned int timeout)
{
    unsigned int len = 0;

    #ifdef COAP_DTLS_SUPPORT
        if(COAP_ENDPOINT_DTLS == network->ep_type)  {
            len = datalen;
            memset(data, 0x00, datalen);
            CoAPNetworkDTLS_read(network->context, data, &len, timeout);
        } else {
    #endif
        memset(data, 0x00, datalen);
        len = HAL_UDP_readTimeout((void *)network->context,
                                  data, COAP_MSG_MAX_PDU_LEN, timeout);
    #ifdef COAP_DTLS_SUPPORT
        }
    #endif
        COAP_TRC("<< CoAP recv %d bytes data\r\n", len);
        return len;
}

unsigned int CoAPNetwork_init(const coap_network_init_t *p_param, coap_network_t *p_network)
{
    unsigned int    err_code = COAP_SUCCESS;

    if(NULL == p_param || NULL == p_network){
        return COAP_ERROR_INVALID_PARAM;
    }

    /* TODO : Parse the url here */
    p_network->ep_type = p_param->ep_type;

#ifdef COAP_DTLS_SUPPORT
    if(COAP_ENDPOINT_DTLS == p_param->ep_type){
        p_network->context = CoAPNetworkDTLS_createSession(p_param->p_host,
                    p_param->port, p_param->p_ca_cert_pem);
        if(NULL == p_network->context){
            return COAP_ERROR_NET_INIT_FAILED;
        }
    }
#endif
    if(COAP_ENDPOINT_NOSEC == p_param->ep_type){
        /*Create udp socket*/
        p_network->context = HAL_UDP_create(p_param->p_host, p_param->port);
        if((void *)-1 == p_network->context){
            return COAP_ERROR_NET_INIT_FAILED;
        }
    }
    return err_code;
}


unsigned int CoAPNetwork_deinit(coap_network_t *p_network)
{
    unsigned int    err_code = COAP_SUCCESS;
    HAL_UDP_close(&p_network->socket_id);
#ifdef COAP_DTLS_SUPPORT
    CoAPNetworkDTLS_freeSession(&p_network->context);
#endif
    return err_code;
}

