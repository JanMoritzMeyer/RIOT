/*
 * CoAP IoT Node - Complete peripheral access via CoAP
 * Based on RIOT gcoap example
 */

#include <stdio.h>
#include <string.h>
#include "msg.h"
#include "shell.h"
#include "xtimer.h"
#include "saul_reg.h"
#include "phydat.h"

#include "net/gcoap.h"
#include "gcoap_example.h"

#define MAIN_QUEUE_SIZE (8)
static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

/* Forward declarations */
extern ssize_t _sensor_temp_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
extern ssize_t _sensor_hum_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);
extern ssize_t _sensors_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len, coap_request_ctx_t *ctx);

/* Test handler shell command */
static int _cmd_test_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <handler>\n", argv[0]);
        printf("Available handlers: temp, hum, sensors\n");
        return 1;
    }

    uint8_t buf[512];
    coap_pkt_t pdu;
    ssize_t resp_len;
    
    printf("\n=== Testing Handler: %s ===\n", argv[1]);
    
    /* Initialize a test GET request */
    char path[64];
    snprintf(path, sizeof(path), "/sensors/%s", argv[1]);
    
    int res = gcoap_req_init(&pdu, buf, sizeof(buf), COAP_METHOD_GET, path);
    if (res < 0) {
        printf("ERROR: Failed to initialize PDU: %d\n", res);
        return 1;
    }
    
    /* Call appropriate handler */
    if (strcmp(argv[1], "temp") == 0) {
        resp_len = _sensor_temp_handler(&pdu, buf, sizeof(buf), NULL);
    } else if (strcmp(argv[1], "hum") == 0) {
        resp_len = _sensor_hum_handler(&pdu, buf, sizeof(buf), NULL);
    } else if (strcmp(argv[1], "sensors") == 0) {
        resp_len = _sensors_handler(&pdu, buf, sizeof(buf), NULL);
    } else {
        printf("ERROR: Unknown handler '%s'\n", argv[1]);
        return 1;
    }
    
    /* Display results */
    if (resp_len > 0) {
        printf("✓ Handler returned %zd bytes\n", resp_len);
        printf("Response code: %u.%02u\n", 
               coap_get_code_class(&pdu), 
               coap_get_code_detail(&pdu));
        
        if (pdu.payload_len > 0) {
            printf("Payload (%u bytes):\n", pdu.payload_len);
            printf("  %.*s\n", (int)pdu.payload_len, (char*)pdu.payload);
        } else {
            printf("No payload\n");
        }
    } else {
        printf("✗ Handler failed with code: %zd\n", resp_len);
    }
    
    return 0;
}

/* Shell command to list and read local sensors via SAUL */
static int _cmd_sensors(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    printf("\n=== Local Sensors (SAUL) ===\n");
    
    saul_reg_t *dev = saul_reg;
    int count = 0;
    
    while (dev) {
        const char *type_name = "unknown";
        switch (dev->driver->type) {
            case SAUL_SENSE_TEMP: type_name = "Temperature"; break;
            case SAUL_SENSE_HUM: type_name = "Humidity"; break;
            case SAUL_SENSE_PRESS: type_name = "Pressure"; break;
            case SAUL_SENSE_LIGHT: type_name = "Light"; break;
            case SAUL_SENSE_ACCEL: type_name = "Accelerometer"; break;
            case SAUL_SENSE_MAG: type_name = "Magnetometer"; break;
            case SAUL_SENSE_GYRO: type_name = "Gyroscope"; break;
        }
        
        printf("[%d] %-20s (Type: %s)\n", count++, dev->name, type_name);
        
        /* Try to read sensor value */
        phydat_t data;
        int res = saul_reg_read(dev, &data);
        if (res > 0) {
            printf("    Value: ");
            for (int i = 0; i < res && i < 3; i++) {
                printf("%d ", data.val[i]);
            }
            printf("\n");
        } else {
            printf("    (Read failed: %d)\n", res);
        }
        
        dev = dev->next;
    }
    
    if (count == 0) {
        printf("No SAUL sensors found!\n");
        printf("Make sure SAUL modules are enabled and sensors are connected.\n");
    } else {
        printf("\nTotal sensors: %d\n", count);
    }
    
    return 0;
}

static const shell_command_t shell_commands[] = {
    { "test", "Test a CoAP handler", _cmd_test_handler },
    { "sensors", "List local SAUL sensors", _cmd_sensors },
    { NULL, NULL, NULL }
};


int main(void)
{
    /* Initialize message queue for main thread */
    msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);
    
    printf("\n");
    puts("====================================");
    puts("    CoAP IoT Node - Peripheral Access");
    puts("    Team Multi-Device Communication");
    puts("====================================");
    printf("Board: %s\n", RIOT_BOARD);
    printf("CoAP Port: %d\n", CONFIG_GCOAP_PORT);
    puts("");

    /* Initialize CoAP server with all IoT handlers */
    printf("Initializing CoAP server...\n");
    server_init();

    /* Wait a moment for SAUL auto-initialization */
    printf("Waiting for SAUL initialization...\n");
    xtimer_sleep(2);

    /* Show available resources */
    puts("\nAvailable CoAP resources:");
    puts("  GET  /riot/board        - Board information");
    puts("  GET  /sensors           - List all sensors");
    puts("  GET  /sensors/temp      - Temperature sensor");
    puts("  GET  /sensors/hum       - Humidity sensor");  
    puts("  GET  /sensors/press     - Pressure sensor");
    puts("  GET  /sensors/light     - Light sensor");
    puts("  GET  /sensors/accel     - Accelerometer");
    puts("  GET  /led               - LED status");
    puts("  PUT  /led               - LED control (on/off/r,g,b)");
    puts("  PUT  /led/color         - LED color (r,g,b)");
    puts("");

    /* Show shell commands */
    puts("Shell commands:");
    puts("  sensors                 - List local sensors");
    puts("  led <cmd>               - Control local LED");
    puts("  get <addr> <path>       - CoAP GET request");
    puts("  put <addr> <path> <val> - CoAP PUT request");
    puts("  scan                    - Scan for other teams");
    puts("  ledbomb <color>         - Set all team LEDs");
    puts("");

    puts("===== IoT Node Ready =====");
    puts("Type 'help' for all commands");
    
    /* Start enhanced shell with IoT commands */
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    /* should never be reached */
    return 0;
}
