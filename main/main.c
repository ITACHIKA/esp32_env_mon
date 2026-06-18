#include "uart_service.h"
#include "option_configure.h"
#include "nvs_service.h"
#include "network_service.h"
#include "mqtt_service.h"
#include "driver/i2c.h"
#include "net_shell.h"
#include "batt_mon.h"
#include "esp_common.h"
#include "sensiron_common.h"

static const char *TAG = "main";

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA 4
#define I2C_MASTER_SCL 5

#define I2C_MASTER_FREQ_HZ 100000

#define MAX_RETRIES_BEFORE_REBOOT 5

#define CRC8_POLYNOMIAL 0x31
#define CRC8_INIT 0xFF

TimerHandle_t scdReadTimer;

TaskHandle_t scd_read_task_handle;

bool fault_flag = false;
bool need_reset = true;
static uint8_t fail_cnt = 0;

static char *mqtt_co2_topic;
static char *mqtt_atemp_topic;
static char *mqtt_rh_topic;
static char *mqtt_battlvl_topic;
static char *mqtt_battraw_topic;

uint8_t sensirion_common_generate_crc(const uint8_t *data, uint16_t count)
{
    uint16_t current_byte;
    uint8_t crc = CRC8_INIT;
    uint8_t crc_bit;
    /* calculates 8-Bit checksum with given polynomial */
    for (current_byte = 0; current_byte < count; ++current_byte)
    {
        crc ^= (data[current_byte]);
        for (crc_bit = 8; crc_bit > 0; --crc_bit)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

static esp_err_t scd_write_command(uint8_t addr,uint16_t cmd)
{
    uint8_t send_seq[2];
    send_seq[0] = cmd >> 8;
    send_seq[1] = cmd;
    return i2c_master_write_to_device(I2C_MASTER_NUM, addr, send_seq, 2, pdMS_TO_TICKS(500));
}

void scd_read_callback()
{
    xTaskNotifyGive(scd_read_task_handle);
}

void scd_read_data(void *pvParameters)
{
    for (;;)
    {
        char result[32];
        // ESP_LOGI(TAG,"scd read");
        uint8_t readBuffer[9] = {0};
        uint8_t counter = 0;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (fail_cnt == MAX_RETRIES_BEFORE_REBOOT)
        {
            // esp_restart();
            ESP_LOGE(TAG, "SCD40 repetedly fail, please manually reset system power.");
            //xTimerStop(scdReadTimer,portMAX_DELAY);
            need_reset = true;
        }
        if (fault_flag && !need_reset)
        {
            fail_cnt++;
            ESP_LOGE(TAG, "SCD40 error, now reinit");
            scd_write_command(SCD40_I2C_ADDR, SCD40_STOP_PERIODIC); // stop periodic
            vTaskDelay(pdMS_TO_TICKS(500));
            scd_write_command(SCD40_I2C_ADDR, SCD40_REINIT); // reinit
            vTaskDelay(pdMS_TO_TICKS(30));
            scd_write_command(SCD40_I2C_ADDR, SCD40_START_PERIODIC);
            xTimerReset(scdReadTimer, portMAX_DELAY);
            fault_flag = false;
            continue;
        }
        if (!fault_flag)
        {
            //read SCD40
            ++counter;
            if(counter==5)
            {
                counter = 0;
                esp_err_t ret;
                ret = scd_write_command(SCD40_I2C_ADDR, SCD40_READ_MEASUREMENT);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Write error: %d", ret);
                    fault_flag = true;
                    continue;
                }
                vTaskDelay(pdMS_TO_TICKS(1)); // according to ds
                ret = i2c_master_read_from_device(I2C_MASTER_NUM, SCD40_I2C_ADDR, readBuffer, 9, pdMS_TO_TICKS(500));
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Read error: %d", ret);
                    fault_flag = true;
                    continue;
                }
                bool co2_crc_res = (sensirion_common_generate_crc(readBuffer, 2) == readBuffer[2]);
                bool temp_crc_res = (sensirion_common_generate_crc(&readBuffer[3], 2) == readBuffer[5]);
                bool rh_crc_res = (sensirion_common_generate_crc(&readBuffer[6], 2) == readBuffer[8]);
                uint16_t co2_ppm = ((uint16_t)readBuffer[0] << 8 | readBuffer[1]);
                uint16_t amb_temp_raw = ((uint16_t)readBuffer[3] << 8 | readBuffer[4]);
                uint16_t rel_humi_raw = ((uint16_t)readBuffer[6] << 8 | readBuffer[7]);
                float amb_temp = -45.0f + 175.0f * ((float)amb_temp_raw / 65535.0f);
                float rel_humi = 100.0f * ((float)rel_humi_raw / 65535.0f);
                if (co2_crc_res)
                {
                    snprintf(result, sizeof(result), "{\"co2\":%u}", co2_ppm);
                    mqtt_publish(mqtt_co2_topic, result);
                }
                if (temp_crc_res)
                {
                    snprintf(result, sizeof(result), "{\"atemp\":%.2f}", amb_temp);
                    mqtt_publish(mqtt_atemp_topic, result);
                }
                if (rh_crc_res)
                {
                    snprintf(result, sizeof(result), "{\"rh\":%.2f}", rel_humi);
                    mqtt_publish(mqtt_rh_topic, result);
                }
            }
        }
        // uint16_t batt_raw=get_batt_voltage();
        // uint8_t batt_level=get_battery_level(batt_raw);
        // snprintf(result, sizeof(result), "{\"battlevel\":%d}", batt_level);
        // mqtt_publish(mqtt_battlvl_topic, result);
        // snprintf(result, sizeof(result), "{\"battraw\":%d}", batt_raw);
        // mqtt_publish(mqtt_battlvl_topic, result);
    }
}

void app_main(void)
{
    // init all components here
    ESP_ERROR_CHECK(nvs_utils_init());
    optionConfigInit();
    uart_init();
    batt_cfg cfg = {
        .adc_channel = ADC_CHANNEL_7,
        .adc_unit = ADC_UNIT_1,
        .pg_pin = 42,
        .s2_pin = 41,
        .s1_pin = 40
    };
    batt_mon_init(cfg);
    networkInit();
    mqtt_init();
    start_webserver();
    // put user code here
    mqtt_co2_topic = calloc(1, 128);
    mqtt_rh_topic = calloc(1, 128);
    mqtt_atemp_topic = calloc(1, 128);
    mqtt_battlvl_topic = calloc(1, 128);
    mqtt_battraw_topic = calloc(1, 128);
    snprintf(mqtt_co2_topic, 128, "sensor/%s/co2", devName);
    snprintf(mqtt_rh_topic, 128, "sensor/%s/rh", devName);
    snprintf(mqtt_atemp_topic, 128, "sensor/%s/atemp", devName);
    snprintf(mqtt_battlvl_topic,128,"sensor/%s/battlvl",devName);
    snprintf(mqtt_battraw_topic,128,"sensor/%s/battraw",devName);
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));

    scdReadTimer = xTimerCreate("scdReadTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, scd_read_callback);
    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for SCD40 ready
    xTaskCreate(scd_read_data, "SCD read Task", 4096, NULL, 5, &scd_read_task_handle);

    scd_write_command(SCD40_I2C_ADDR, SCD40_START_PERIODIC); // start periodic measurement
    xTimerStart(scdReadTimer, portMAX_DELAY);
}