
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/gcoap.h"
#include "net/sock/udp.h"
#include "net/sock/util.h"
#include "od.h"
#include "shell.h"
#include "uri_parser.h"

#include "gcoap_example.h"

#define ENABLE_DEBUG 0
#include "debug.h"

#if IS_USED(MODULE_GCOAP_DTLS)
#include "net/dsm.h"
#endif

#ifndef CONFIG_URI_MAX
#define CONFIG_URI_MAX      128
#endif

// static sock_udp_ep_t _proxy_remote;
static char _proxy_uri[CONFIG_URI_MAX];

/* Retain request URI to re-request if response includes block. User must not
 * start a new request (with a new path) until any blockwise transfer
 * completes or times out. */
static char _last_req_uri[CONFIG_URI_MAX];

/* Last remote endpoint where an Observe request has been sent to */
// static sock_udp_ep_t obs_remote;

/* the token used for observing a remote resource */
// static uint8_t obs_req_token[GCOAP_TOKENLEN_MAX];

/* actual length of above token */
// static size_t obs_req_tkl = 0;

uint16_t req_count = 0;


static gcoap_socket_type_t _get_tl(const char *uri);
static ssize_t _send(uint8_t *buf, size_t len, const sock_udp_ep_t *remote, void *ctx, gcoap_socket_type_t tl);

/*
 * Response callback.
 */
static void _resp_handler(const gcoap_request_memo_t *memo, coap_pkt_t* pdu,
                          const sock_udp_ep_t *remote)
{
    (void)remote;       /* not interested in the source currently */

    if (memo->state == GCOAP_MEMO_TIMEOUT) {
        printf("gcoap: timeout for msg ID %02u\n", coap_get_id(pdu));
        return;
    }
    else if (memo->state == GCOAP_MEMO_RESP_TRUNC) {
        /* The right thing to do here would be to look into whether at least
         * the options are complete, then to mentally trim the payload to the
         * next block boundary and pretend it was sent as a Block2 of that
         * size. */
        printf("gcoap: warning, incomplete response; continuing with the truncated payload\n");
    }
    else if (memo->state != GCOAP_MEMO_RESP) {
        printf("gcoap: error in response\n");
        return;
    }

    coap_block1_t block;
    if (coap_get_block2(pdu, &block) && block.blknum == 0) {
        puts("--- blockwise start ---");
    }

    char *class_str = (coap_get_code_class(pdu) == COAP_CLASS_SUCCESS)
                            ? "Success" : "Error";
    printf("gcoap: response %s, code %1u.%02u", class_str,
                                                coap_get_code_class(pdu),
                                                coap_get_code_detail(pdu));
    if (pdu->payload_len) {
        unsigned content_type = coap_get_content_type(pdu);
        if (content_type == COAP_FORMAT_TEXT
                || content_type == COAP_FORMAT_LINK
                || coap_get_code_class(pdu) == COAP_CLASS_CLIENT_FAILURE
                || coap_get_code_class(pdu) == COAP_CLASS_SERVER_FAILURE) {
            /* Expecting diagnostic payload in failure cases */
            printf(", %u bytes\n%.*s\n", pdu->payload_len, pdu->payload_len,
                                                          (char *)pdu->payload);
        }
        else {
            printf(", %u bytes\n", pdu->payload_len);
            od_hex_dump(pdu->payload, pdu->payload_len, OD_WIDTH_DEFAULT);
        }
    }
    else {
        printf(", empty payload\n");
    }

    /* ask for next block if present */
    if (coap_get_block2(pdu, &block)) {
        if (block.more) {
            unsigned msg_type = coap_get_type(pdu);
            if (block.blknum == 0 && !strlen(_last_req_uri)) {
                puts("Path too long; can't complete blockwise");
                return;
            }
            uri_parser_result_t urip;
            uri_parser_process(&urip, _last_req_uri, strlen(_last_req_uri));
            if (*_proxy_uri) {
                gcoap_req_init(pdu, (uint8_t *)pdu->hdr, CONFIG_GCOAP_PDU_BUF_SIZE,
                               COAP_METHOD_GET, NULL);
            }
            else {
                gcoap_req_init(pdu, (uint8_t *)pdu->hdr, CONFIG_GCOAP_PDU_BUF_SIZE,
                               COAP_METHOD_GET, urip.path);
            }

            if (msg_type == COAP_TYPE_ACK) {
                coap_hdr_set_type(pdu->hdr, COAP_TYPE_CON);
            }
            block.blknum++;
            coap_opt_add_block2_control(pdu, &block);

            if (*_proxy_uri) {
                coap_opt_add_proxy_uri(pdu, urip.scheme);
            }

            int len = coap_opt_finish(pdu, COAP_OPT_FINISH_NONE);
            gcoap_socket_type_t tl = _get_tl(*_proxy_uri ? _proxy_uri : _last_req_uri);
            _send((uint8_t *)pdu->hdr, len, remote, memo->context, tl);
        }
        else {
            puts("--- blockwise complete ---");
        }
    }
}

static gcoap_socket_type_t _get_tl(const char *uri)
{
    if (!strncmp(uri, "coaps", 5)) {
        return GCOAP_SOCKET_TYPE_DTLS;
    }
    else if (!strncmp(uri, "coap", 4)) {
        return GCOAP_SOCKET_TYPE_UDP;
    }
    return GCOAP_SOCKET_TYPE_UNDEF;
}

static ssize_t _send(uint8_t *buf, size_t len, const sock_udp_ep_t *remote,
                     void *ctx, gcoap_socket_type_t tl)
{
    ssize_t bytes_sent = gcoap_req_send(buf, len, remote, NULL, _resp_handler, ctx, tl);
    if (bytes_sent > 0) {
        req_count++;
    }
    return bytes_sent;
}


static int _cmd_coap_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    printf("CoAP port: %u\n", CONFIG_GCOAP_PORT);
    printf("CoAP server running\n");
    return 0;
}


static int _cmd_coap_get(int argc, char **argv)
{
   (void)argc;
    (void)argv;
    
    printf("CoAP port: %u\n", CONFIG_GCOAP_PORT);
    printf("CoAP server running\n");
    return 0;
}

static int _cmd_coap_put(int argc, char **argv)
{
    // if (argc < 3) {
    //     printf("usage: %s <addr> <uri>\n", argv[0]);
    //     return 1;
    // }

    // // coap_pkt_t pdu;
    // // size_t len;
    // // sock_udp_ep_t remote;

    // return 0;

    (void)argc;
    (void)argv;
    
    printf("CoAP port: %u\n", CONFIG_GCOAP_PORT);
    printf("CoAP server running\n");
    return 0;
}

static const shell_command_t shell_commands[] = {
    {"get", "CoAP GET request", _cmd_coap_get},
    {"put", "CoAP PUT request", _cmd_coap_put},
    {"info", "Display CoAP client info", _cmd_coap_info},
    { NULL, NULL, NULL }
};

void gcoap_cli_init(void)
{
    /* CoAP client is initialized via gcoap module */
    printf("CoAP client ready\n");
    shell_run(shell_commands, NULL, 0);
}

SHELL_COMMAND(put, "CoAP PUT ", _cmd_coap_put);
SHELL_COMMAND(get, "CoAP GET ", _cmd_coap_get);
SHELL_COMMAND(info, "CoAP client info", _cmd_coap_info);