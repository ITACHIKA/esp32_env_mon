#include "esp_wifi.h"
#include "nvs_service.h"
#include "option_configure.h"
#include "esp_event.h"
#include "esp_common.h"

#define WIFI_RECONN_INTV_MS 50

#define WIFI_RECONN_MAX_RETRIES 10

const char *TAG = "NET";
bool networkReady = false;
static uint8_t retry_count=0;

static void wifiEventHandler(void *handlerargs, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            // esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to AP");
            networkReady = true;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "Disconnected from SSID:%s, reason:%d", disconn->ssid, disconn->reason);
            networkReady = false;
            retry_count++;
            if(retry_count<WIFI_RECONN_MAX_RETRIES)
            {
                vTaskDelay(pdMS_TO_TICKS(WIFI_RECONN_INTV_MS));
                esp_wifi_connect();
            }
            else
            {
                ESP_LOGE(TAG,"WIFI connect retry max count reached, please check configuration!");
            }
            break;
        }
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t networkInit(void)
{
    esp_err_t ret;

    if (strcmp(wifiSSID, "") == 0)
    {
        ESP_LOGE(TAG,"Wifi config invalid. Please reset.");
        return ESP_ERR_INVALID_ARG;
    }
    else
    {
        ESP_LOGI(TAG,"wifissid:%s", wifiSSID);
        //esp_rom_printf("wifipass:%s\n", WifiPasswd);
        ret = esp_netif_init();
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Netif init unsuccessful on esp_netif_init(): %s", esp_err_to_name(ret));
            return ret;
        }
        ret = esp_event_loop_create_default();
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Netif init unsuccessful on esp_event_loop_create_default(): %s", esp_err_to_name(ret));
            return ret;
        }
        esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
        if(sta_netif == NULL)
        {
            ESP_LOGE(TAG,"Netif init unsuccessful on esp_netif_create_default_wifi_sta().");
            return ESP_FAIL;
        }
        ret = esp_netif_set_hostname(sta_netif, devName);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Netif init unsuccessful on esp_netif_set_hostname(): %s", esp_err_to_name(ret));
            return ret;
        }
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ret = esp_wifi_init(&cfg);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on esp_wifi_init(): %s", esp_err_to_name(ret));
            return ret;
        }
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on esp_wifi_set_mode(): %s", esp_err_to_name(ret));
            return ret;
        }

        ret = esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifiEventHandler,
            NULL,
            NULL);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on WIFI_EVENT register: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifiEventHandler,
            NULL,
            NULL);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on IP_EVENT register: %s", esp_err_to_name(ret));
            return ret;
        }

        wifi_config_t wifi_cfg = {0};
        wifi_cfg.sta.threshold.authmode=WIFI_AUTH_OPEN;
        strncpy((char *)wifi_cfg.sta.ssid, wifiSSID, sizeof(wifi_cfg.sta.ssid));
        wifi_cfg.sta.ssid[sizeof(wifi_cfg.sta.ssid) - 1] = '\0';
        strncpy((char *)wifi_cfg.sta.password, WifiPasswd, sizeof(wifi_cfg.sta.password));
        wifi_cfg.sta.password[sizeof(wifi_cfg.sta.password) - 1] = '\0'; //to avoid ovf
        ret = esp_wifi_set_ps(WIFI_PS_NONE);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on esp_wifi_set_ps(): %s", esp_err_to_name(ret));
            return ret;
        }
        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on esp_wifi_set_config(): %s", esp_err_to_name(ret));
            return ret;
        }
        ret = esp_wifi_start();
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on esp_wifi_start(): %s", esp_err_to_name(ret));
            return ret;
        }
        
        ret = esp_wifi_set_max_tx_power(44); //https://esp32.com/viewtopic.php?f=2&t=41899#p137764, guess for YD-ESP32-S3 or similar clones only
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on esp_wifi_set_max_tx_power(): %s", esp_err_to_name(ret));
            return ret;
        }
        
        ret = esp_wifi_connect();
        if(ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Wifi init unsuccessful on esp_wifi_connect(): %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG,"wifi connect: %d", ret);
    }
    return ESP_OK;
}
