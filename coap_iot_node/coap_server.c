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

 int _led_value_RGB[3] = {0, 0, 0}; // R, G, B

static saul_reg_t* _find_saul_device(uint8_t type);
static int _read_sensor_value(uint8_t type, phydat_t *data);
// static int _write_led_value(uint8_t type, phydat_t *data);

static ssize_t _encode_link(const coap_resource_t *resource, char *buf, size_t maxlen, coap_link_encoder_ctx_t *context);
static ssize_t _stats_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _riot_board_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);

static ssize_t _info_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _sensor_accel_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _sensor_hum_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _sensor_light_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _sensor_press_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _sensor_temp_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);

static ssize_t _led_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _led_color_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _led_usage_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _led_get_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _led_put_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _led_color_put_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
static ssize_t _led_color_get_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);

static saul_reg_t* _find_saul_device_by_name(const char *name);
static ssize_t _devices_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);


/* CoAP resources for IoT peripherals - Must be sorted by path (ASCII order) */
static const coap_resource_t _resources[] = {
    { "/cli/stats", COAP_GET | COAP_PUT, _stats_handler, NULL },
    { "/led", COAP_GET | COAP_PUT, _led_handler, NULL },
    { "/led/color", COAP_GET | COAP_PUT, _led_color_handler, NULL },
    { "/led/usage", COAP_GET, _led_usage_handler, NULL },
    { "/riot/board", COAP_GET, _riot_board_handler, NULL },
    { "/info", COAP_GET, _info_handler, NULL },
    { "/sensors/accel", COAP_GET, _sensor_accel_handler, NULL },
    { "/sensors/hum", COAP_GET, _sensor_hum_handler, NULL },
    { "/sensors/light", COAP_GET, _sensor_light_handler, NULL },
    { "/sensors/press", COAP_GET, _sensor_press_handler, NULL },
    { "/sensors/temp", COAP_GET, _sensor_temp_handler, NULL },
    { "/devices", COAP_GET, _devices_handler, NULL }, 
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


static ssize_t _devices_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    (void)pdu;
    
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    size_t pos = 0;
    
    pos += snprintf(response + pos, len - resp_len - pos, "Available SAUL devices:\n");
    
    saul_reg_t *dev = saul_reg;
    while (dev && pos < len - resp_len - 50) {
        pos += snprintf(response + pos, len - resp_len - pos,
                       "- %s (type: %d)\n",
                       dev->name ? dev->name : "unnamed",
                       dev->driver->type);
        dev = dev->next;
    }
    
    return resp_len + pos;
}





static saul_reg_t* _find_saul_device_by_name(const char *name) {
    saul_reg_t *dev = saul_reg;
    while (dev) {
        // printf("Checking device: %s, %d\n", dev->name, strcmp(dev->name, name));
        if (dev->name && strcmp(dev->name, name) == 0) {

            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}



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


// static int _write_led_value(uint8_t type, phydat_t *data) {
//     saul_reg_t *dev = _find_saul_device(type);
//     if (!dev) {
//         DEBUG("LED type %d not found\n", type);
//         return -1;
//     }
    
//     int res = saul_reg_write(dev, data);
//     if (res < 0) {
//         DEBUG("Failed to write LED: %d\n", res);
//         return res;
//     }
    
//     return res;
// }

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


static ssize_t _stats_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx)
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


static ssize_t _riot_board_handler(coap_pkt_t *pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx)
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


static ssize_t _info_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    size_t pos = snprintf(response, len - resp_len,
        "Sensors: /sensors/{temp,hum,press,light,accel}\n"
        "LED: /led, /led/color, /led/usage\n"
    );
   


   return resp_len + pos ;
}

static ssize_t _led_usage_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    (void)pdu;
    
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    size_t pos = snprintf(response, len - resp_len,
        "LED Usage:\n"
        "GET /led - Get LED state (0=off, 1=on)\n"
        "PUT /led <red|blue>,<0|1> - Set LED state\n"
        "GET /led/color - Get RGB color\n"
        "PUT /led/color <R,G,B> - Set RGB color (0-255)\n"
    );
    return resp_len + pos;
}


static ssize_t _sensor_temp_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
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

static ssize_t _sensor_hum_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
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

static ssize_t _sensor_press_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
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

static ssize_t _sensor_light_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
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

static ssize_t _sensor_accel_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
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

static ssize_t _led_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;

    unsigned method_flag = coap_method2flag(coap_get_code_detail(pdu));



    switch (method_flag) {
        case COAP_GET:
            return _led_get_handler(pdu, buf, len, ctx);
        case COAP_PUT:
             return _led_put_handler(pdu, buf, len, ctx);
        default:
            return gcoap_response(pdu, buf, len, COAP_CODE_METHOD_NOT_ALLOWED);
    }

}

static ssize_t _led_get_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;
    phydat_t data;
    // int result = _read_sensor_value(SAUL_ACT_SWITCH, &data);
    saul_reg_t* dev = _find_saul_device_by_name("LED Blue (Conn)");
    // if (result < 0) {
    //     return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    // }
    if (!dev) {
        DEBUG("gcoap_cli: LED Blue (Conn) not found\n");
        return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    }
    int result = saul_reg_read(dev, &data);
    if (result < 0) {
        DEBUG("gcoap_cli: error reading LED state\n");
        return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    }   
    size_t pos = snprintf(response, len - resp_len, "LED state: %d\n", data.val[0]);
    return resp_len + pos;

}    

static ssize_t _led_put_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;

    phydat_t data;
    

    if (pdu->payload_len == 0) {
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }
    // Payload in String umwandeln
    char payload[32] = { 0 };
    size_t copy_len = pdu->payload_len < sizeof(payload) - 1 ? pdu->payload_len : sizeof(payload) - 1;
    memcpy(payload, (char *)pdu->payload, copy_len);
    payload[copy_len] = '\0';
    printf("%s\n", payload);
    printf("Payload length: %d\n", pdu->payload_len);
   
    // Format: "red,1", "blue,0", oder nur "1", "0"
    char led_name[20] = "LED Blue (Conn)";  // Default
    int led_value = -1;

    if (strncmp(payload, "red,", 4) == 0) {
        led_value = atoi(payload + 4);
        strcpy(led_name, "LED Red (D13)");
        printf("Red LED selected. led_value: %d\n", led_value);
    }
    else if (strncmp(payload, "blue,", 5) == 0) {
    
        led_value = atoi(payload + 5);
        strcpy(led_name, "LED Blue (Conn)");
        printf("Blue LED selected. led_value: %d\n", led_value);
    }
    else if (payload[0] == '0' || payload[0] == '1') {

        led_value = payload[0] - '0';
        printf("Default (Blue) LED selected. led_value: %d\n", led_value);
    }
    else {
        printf("Invalid LED format. Expected: red,1 or blue,0 or 0/1\n");
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }

    
    if (led_value != 0 && led_value != 1) {
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }

    data.val[0] = led_value;

    // LED Device finden und schreiben
    saul_reg_t* dev = _find_saul_device_by_name(led_name);
    if (!dev) {
        DEBUG("gcoap_cli: %s not found\n", led_name);
        return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }

    int result = saul_reg_write(dev, &data);
    if (result < 0) {
        DEBUG("gcoap_cli: error writing LED: %d\n", result);
        return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }
    

    gcoap_resp_init(pdu, buf, len, COAP_CODE_CHANGED);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);
    char *response = (char *)pdu->payload;


    size_t pos = snprintf(response, len - resp_len,  "LED state set to: %d\n", data.val[0]);
    return resp_len + pos;
}


static ssize_t _led_color_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;

    unsigned method_flag = coap_method2flag(coap_get_code_detail(pdu));

    switch (method_flag) {
        case COAP_GET:
            return _led_color_get_handler(pdu, buf, len, ctx);
        case COAP_PUT:
             return _led_color_put_handler(pdu, buf, len, ctx);
        default:
            return gcoap_response(pdu, buf, len, COAP_CODE_METHOD_NOT_ALLOWED);
    }
}

static ssize_t _led_color_get_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);

    char *response = (char *)pdu->payload;

    size_t pos = snprintf(response, len - resp_len, 
                         "LED color - R:%d G:%d B:%d\n", 
                         _led_value_RGB[0], _led_value_RGB[1], _led_value_RGB[2]);
    return resp_len + pos;
}

static ssize_t _led_color_put_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx){
    (void)ctx;
    
    phydat_t data;
   

    // Validierung ERST (bevor pdu modifiziert wird!)
    if (pdu->payload_len == 0) {
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }

    // Payload VORHER lesen (bevor gcoap_resp_init das pdu überschreibt!)
    char payload[32] = { 0 };
    size_t copy_len = pdu->payload_len < sizeof(payload) - 1 ? pdu->payload_len : sizeof(payload) - 1;
    memcpy(payload, (char *)pdu->payload, copy_len);
    payload[copy_len] = '\0';
    printf("Received payload: %s\n", payload);

    // Manuelles Parsing (sscanf kann auf embedded Systemen crashen)
    char *ptr = payload;
    int comma_count = 0;
    
    // Parse R
    _led_value_RGB[0] = atoi(ptr);
    printf("Parsed R: %d\n", _led_value_RGB[0]);
    
    // Finde erstes Komma
    while (*ptr && *ptr != ',') ptr++;
    if (*ptr == ',') { ptr++; comma_count++; } else goto parse_error;
    
    // Parse G
    _led_value_RGB[1] = atoi(ptr);
    printf("Parsed G: %d\n", _led_value_RGB[1]);
    
    // Finde zweites Komma
    while (*ptr && *ptr != ',') ptr++;
    if (*ptr == ',') { ptr++; comma_count++; } else goto parse_error;
    
    // Parse B
    _led_value_RGB[2] = atoi(ptr);
    printf("Parsed B: %d\n", _led_value_RGB[2]);
    
    if (comma_count != 2) {
        parse_error:
        printf("Invalid LED color format (expected R,G,B)\n");
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }
    
    printf("LED color parsed - R:%d G:%d B:%d\n", _led_value_RGB[0], _led_value_RGB[1], _led_value_RGB[2]);

    saul_reg_t* dev = _find_saul_device_by_name("WS281X RGB LED");
    if (!dev) {
        printf("gcoap_cli: WS281X RGB LED not found\n");
        return gcoap_response(pdu, buf, len, COAP_CODE_PATH_NOT_FOUND);
    }

    // Validiere Wertebereich 0-255
    if (_led_value_RGB[0] < 0 || _led_value_RGB[0] > 255 ||
        _led_value_RGB[1] < 0 || _led_value_RGB[1] > 255 ||
        _led_value_RGB[2] < 0 || _led_value_RGB[2] > 255) {
        printf("LED color values out of range (0-255)\n");
        return gcoap_response(pdu, buf, len, COAP_CODE_BAD_REQUEST);
    }

    data.val[0] = (int16_t)_led_value_RGB[0];
    data.val[1] = (int16_t)_led_value_RGB[1];
    data.val[2] = (int16_t)_led_value_RGB[2];

    printf("Writing to LED: R:%d G:%d B:%d\n", data.val[0], data.val[1], data.val[2]);

    int result = saul_reg_write(dev, &data);
    if (result < 0) {
        printf("gcoap_cli: error writing LED color: %d\n", result);
        return gcoap_response(pdu, buf, len, COAP_CODE_INTERNAL_SERVER_ERROR);
    }
    printf("LED write successful, result: %d\n", result);
    
    // Jetzt erst Response aufbauen!
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CHANGED);
    coap_opt_add_format(pdu, COAP_FORMAT_TEXT);
    size_t resp_len = coap_opt_finish(pdu, COAP_OPT_FINISH_PAYLOAD);
    
    // Response-Pointer NACH gcoap_resp_init holen!
    char *response = (char *)pdu->payload;
    size_t pos = snprintf(response, len - resp_len, 
                         "LED color set to R:%d G:%d B:%d\n", 
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
