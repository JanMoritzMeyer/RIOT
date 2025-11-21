#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "event/periodic_callback.h"
#include "event/thread.h"
#include "fmt.h"
#include "net/gcoap.h"
#include "net/utils.h"
#include "od.h"
#include "periph/rtc.h"
#include "shell.h"
#include "time_units.h"
#include "saul.h"
#include "saul_reg.h"
#include "phydat.h"

#include "gcoap_example.h"

#define ENABLE_DEBUG 0
#include "debug.h"

// #if IS_USED(MODULE_GCOAP_DTLS)
// #include "net/credman.h"
// #include "net/dsm.h"
// #include "tinydtls_keys.h"

static ssize_t _encode_link(const coap_resource_t *resource, char *buf,
                            size_t maxlen, coap_link_encoder_ctx_t *context);
ssize_t _stats_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
ssize_t _riot_board_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
ssize_t _sensors_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
ssize_t _sensor_accel_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
ssize_t _sensor_hum_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
ssize_t _sensor_light_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
ssize_t _sensor_press_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
ssize_t _sensor_temp_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
// static ssize_t _led_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
// static ssize_t _led_color_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);

static saul_reg_t* _find_saul_device(uint8_t type);
static int _read_sensor_value(uint8_t type, phydat_t *data);

/* CoAP resources for IoT peripherals - Must be sorted by path (ASCII order) */
static const coap_resource_t _resources[] = {
    { "/cli/stats", COAP_GET | COAP_PUT, _stats_handler, NULL },
    // { "/led", COAP_GET | COAP_PUT, _led_handler, NULL },
    // { "/led/color", COAP_GET | COAP_PUT, _led_color_handler, NULL },
    { "/riot/board", COAP_GET, _riot_board_handler, NULL },
    { "/sensors", COAP_GET, _sensors_handler, NULL },
    { "/sensors/accel", COAP_GET, _sensor_accel_handler, NULL },
    { "/sensors/hum", COAP_GET, _sensor_hum_handler, NULL },
    { "/sensors/light", COAP_GET, _sensor_light_handler, NULL },
    { "/sensors/press", COAP_GET, _sensor_press_handler, NULL },
    { "/sensors/temp", COAP_GET, _sensor_temp_handler, NULL },
};

static const char *_link_params[] = {
    ";ct=0;rt=\"count\";obs",
    NULL
};

static gcoap_listener_t _listener = {
    &_resources[0],
    ARRAY_SIZE(_resources),
    GCOAP_SOCKET_TYPE_UNDEF,
    _encode_link,
    NULL,
    NULL
};

static saul_reg_t* _find_saul_device(uint8_t type) {
    saul_reg_t *dev = saul_reg;
    while (dev) {
        if (dev->driver->type == type) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

/* Read sensor value via SAUL interface */
static int _read_sensor_value(uint8_t type, phydat_t *data) {
    saul_reg_t *dev = _find_saul_device(type);
    if (!dev) {
        DEBUG("Sensor type %d not found\n", type);
        return -1;
    }
    
    int res = saul_reg_read(dev, data);
    if (res < 0) {
        DEBUG("Failed to read sensor: %d\n", res);
        return res;
    }
    
    return res;
}

/* Adds link format params to resource list */
static ssize_t _encode_link(const coap_resource_t *resource, char *buf,
                            size_t maxlen, coap_link_encoder_ctx_t *context) {
    ssize_t res = gcoap_encode_link(resource, buf, maxlen, context);
    if (res > 0) {
        if (_link_params[context->link_pos]
                && (strlen(_link_params[context->link_pos]) < (maxlen - res))) {
            if (buf) {
                memcpy(buf+res, _link_params[context->link_pos],
                       strlen(_link_params[context->link_pos]));
            }
            return res + strlen(_link_params[context->link_pos]);
        }
    }

    return res;
}


ssize_t _stats_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx)
{
    (void)ctx;

    /* read coap method type in packet */
    unsigned method_flag = coap_method2flag(coap_get_code_detail(pdu));

    switch (method_flag) {
        case COAP_GET:
            gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
            coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
            size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

            /* write the response buffer with the request count value */
            resp_len += fmt_u16_dec((char *)pdu->payload, req_count);
            return resp_len;

        case COAP_PUT:
            /* convert the payload to an integer and update the internal
               value */
            if (pdu->payload_len <= 5) {
                char payload[6] = { 0 };
                memcpy(payload, (char *)pdu->payload, pdu->payload_len);
                req_count = (uint16_t)strtoul(payload, NULL, 10);
                return gcoap_response(pdu, buf, len, COAP_CODE_CHANGED);
            }
            else {
                return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
            }
    }

    return 0;
}


ssize_t _riot_board_handler(coap_pkt_t *pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx)
{
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    /* write the RIOT board name in the response buffer */
    if (pdu->payload_len >= strlen(RIOT_BOARD)) {
        memcpy(pdu->payload, RIOT_BOARD, strlen(RIOT_BOARD));
        return resp_len + strlen(RIOT_BOARD);
    }
    else {
        puts("gcoap_cli: msg buffer too small");
        return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }
}


ssize_t _sensors_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    const char* sensor_list =
        "Available sensors:\n"
        "Access specific sensor data at /sensors/<type>\n"
        "temp - Temperature Sensor\n"
        "hum  - Humidity Sensor\n"
        "press - Pressure Sensor\n"
        "light - Light Sensor\n"
        "accel - Accelerometer\n"
        "Available LED control at /led and /led/color\n"
        "LED control supports GET and PUT methods.\n";


   memcpy(pdu->payload, sensor_list, strlen(sensor_list));
   return resp_len + strlen(sensor_list);
}

ssize_t _sensor_temp_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    phydat_t data;

    int result = _read_sensor_value(SAUL_SENSE_TEMP, &data);
    if (result < 0) {
        return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    }

    size_t pos = snprintf(response, len - resp_len, "%d.%d°C",
                         data.val[0] / 100, abs(data.val[0]) % 100);
    return resp_len + pos;
}

ssize_t _sensor_hum_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    phydat_t data;

    int result = _read_sensor_value(SAUL_SENSE_HUM, &data);
    if (result < 0) {
        return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    }

    size_t pos = snprintf(response, len - resp_len, "%d.%d%%",
                         data.val[0] / 100, abs(data.val[0]) % 100);
    return resp_len + pos;
}

ssize_t _sensor_press_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    phydat_t data;

    int result = _read_sensor_value(SAUL_SENSE_PRESS, &data);
    if (result < 0) {
        return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    }

    size_t pos = snprintf(response, len - resp_len, "%d Pa",
                         data.val[0]);
    return resp_len + pos;
}

ssize_t _sensor_light_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    phydat_t data;

    int result = _read_sensor_value(SAUL_SENSE_LIGHT, &data);
    if (result < 0) {
        return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    }

    size_t pos = snprintf(response, len - resp_len, "%d lux",
                         data.val[0]);
    return resp_len + pos;
}

ssize_t _sensor_accel_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    phydat_t data;

    int result = _read_sensor_value(SAUL_SENSE_ACCEL, &data);
    if (result < 0) {
        return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    }

    /* Multi-dimensional data (x, y, z) */
    size_t pos = snprintf(response, len - resp_len,
                         "x:%d, y:%d, z:%d",
                         data.val[0], data.val[1], data.val[2]);
    return resp_len + pos;
}


void notify_observers(void)
{
    size_t len;
    uint8_t buf[CONFIG_GCOAP_PDU_BUF_SIZE];
    coap_pkt_t pdu;

    /* send Observe notification for /cli/stats */
    switch (gcoap_obs_init(&pdu, &buf[0], CONFIG_GCOAP_PDU_BUF_SIZE,
            &_resources[0])) {
    case GCOAP_OBS_INIT_OK:
        DEBUG("gcoap_cli: creating /cli/stats notification\n");
        coap_opt_add_format(&pdu, COAP_FORMAT_TEXT);
        len = coap_opt_finish(&pdu, COAP_OPT_FINISH_PAYLOAD);
        len += fmt_u16_dec((char *)pdu.payload, req_count);
        gcoap_obs_send(&buf[0], len, &_resources[0]);
        break;
    case GCOAP_OBS_INIT_UNUSED:
        DEBUG("gcoap_cli: no observer for /cli/stats\n");
        break;
    case GCOAP_OBS_INIT_ERR:
        DEBUG("gcoap_cli: error initializing /cli/stats notification\n");
        break;
    }
}

void server_init(void){
    gcoap_register_listener(&_listener);
}
