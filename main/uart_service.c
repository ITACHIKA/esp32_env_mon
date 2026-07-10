#include <driver/uart.h>
#include <driver/gpio.h>
#include <option_configure.h>
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "esp_common.h"

// #define USB_CDC

#define UART_NUM UART_NUM_0 // UART0
#define UART_TX_PIN 43
#define UART_RX_PIN 44
#define BUF_SIZE 1024
#define RD_BUF_SIZE 8

#define ESP_CONSOLE

static const char* TAG="UART";
static char* REPL_CHAR;

static QueueHandle_t uartEventQueue;

/* void uartEventQueueHandler(void *pvParameters)
{
    char *stringBuffer = malloc(64);
    size_t buffLen = 0;
    uart_event_t event;
    uint8_t dtmp;
    for (;;)
    {
        if (xQueueReceive(uartEventQueue, (void *)&event, portMAX_DELAY))
        {
            switch (event.type)
            {
            case UART_DATA:
                size_t len = uart_read_bytes(UART_NUM, &dtmp, event.size, portMAX_DELAY);
                if (len > 0)
                {
                    uart_write_bytes(UART_NUM, &dtmp, sizeof(dtmp));
                    if (dtmp == '\r' || dtmp == '\n')
                    {
                        esp_rom_printf("\r\n");
                        stringBuffer[buffLen] = '\0';
                        char *stringCopy = malloc(64);
                        strcpy(stringCopy, stringBuffer);
                        xQueueSend(uartInputQueue, &stringCopy, portMAX_DELAY);
                        buffLen = 0;
                    }
                    else if (dtmp == 0x08 || dtmp == 0x7F)
                    {
                        if (buffLen > 0)
                        {
                            buffLen--;
                            stringBuffer[buffLen] = '\0';
                        }
                        continue;
                    }
                    else
                    {
                        stringBuffer[buffLen++] = dtmp;
                    }
                }
                break;
            case UART_FIFO_OVF:
                uart_flush_input(UART_NUM);
                xQueueReset(uartEventQueue);
                break;
            default:
                break;
            }
        }
    }
    free(stringBuffer);
}*/
//this function is currently not used since UART input is managed by ESP Console

void uart_init()
{
#ifndef ESP_CONSOLE
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(UART_NUM, &uart_config);

    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 16, &uartEventQueue, 0);
    xTaskCreate(uartEventQueueHandler, "uartEventQueueHandler", 4096, NULL, 4, NULL);
#else
    esp_console_config_t console_config={
        .max_cmdline_length = 128, //!< length of command line buffer, in bytes
        .max_cmdline_args = 3,    //!< maximum number of command line arguments to parse
        .heap_alloc_caps = MALLOC_CAP_DEFAULT,  //!< where to (e.g. MALLOC_CAP_SPIRAM) allocate heap objects such as cmds used by esp_console
        .hint_color = 32,             //!< ASCII color code of hint text
        .hint_bold = 0,              //!< Set to 1 to print hint text in bold
    }; 
    // linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);
    // printf("config addr=%p  hint_color=%d\n", &console_config, console_config.hint_color);
    // printf("%d",esp_console_init(&console_config));
    // above code will cause crash since REPL will init console again
    // lazy to write REPL function so using default below, which doesn't support hint text color
    esp_console_register_help_command();
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    REPL_CHAR = malloc(strlen(devName) + 2);
    strcpy(REPL_CHAR, devName);
    repl_config.prompt = strcat(REPL_CHAR, ">");
    repl_config.max_cmdline_length = 128;
    esp_console_dev_uart_config_t hw_config = {
    .channel = UART_NUM,    \
    .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE, \
    .tx_gpio_num = UART_TX_PIN,                    \
    .rx_gpio_num = UART_RX_PIN,                    \
    };
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
#endif
    ESP_LOGI(TAG,"UART init done");
}

void uartSend(const char *str)
{
    if (str != NULL)
    {
        uart_write_bytes(UART_NUM, str, strlen(str));
    }
}