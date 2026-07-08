#include <stdio.h>
#include "mqtt_client.h"
#include "option_configure.h"
#include "network_service.h"
#include "esp_common.h"

esp_mqtt_client_handle_t mqttclient;

static const char *TAG = "MQTT";

#define MQTT_MAX_ERR_CNT 15
static uint8_t mqtt_err_cnt = 0;

esp_err_t mqtt_init()
{
    esp_mqtt_client_config_t config = {
        .broker.address.uri = mqttUri,
        //.broker.address.uri="mqtt://192.168.1.100:1883",
    };
    if (strcmp(mqttUsername, "") != 0)
    {
        config.credentials.username = mqttUsername;
    }
    if (strcmp(mqttPasswd, "") != 0)
    {
        config.credentials.authentication.password = mqttPasswd;
    }
    if (strcmp(mqttcliid, "") != 0)
    {
        config.credentials.client_id = mqttcliid;
    }

    mqttclient = esp_mqtt_client_init(&config);
    if (!mqttclient)
    {
        while (!mqttclient)
        {
            ESP_LOGE(TAG, "Retry init mqtt client");
            vTaskDelay(pdMS_TO_TICKS(500));
            mqttclient = esp_mqtt_client_init(&config);
            mqtt_err_cnt++;
            if (mqtt_err_cnt == MQTT_MAX_ERR_CNT)
            {
                break;
            }
        }
        ESP_LOGE(TAG, "mqtt client init fail.");
        return ESP_FAIL;
    }
    esp_mqtt_client_start(mqttclient);
    return ESP_OK;
}

esp_err_t mqtt_publish(const char *topic, const char *msg)
{
    if (networkReady)
    {
        int ret_code = esp_mqtt_client_publish(mqttclient, topic, msg, 0, 1, 0);
        if (ret_code == -1 || ret_code == -2)
        {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}