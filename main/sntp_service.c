#include "sntp_service.h"

#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

#define SNTP_SERVER "pool.ntp.org"
#define SNTP_SYNC_TIMEOUT_MS 30000

static const char *TAG = "SNTP";

static bool sntp_started = false;
static bool time_synced = false;

static void sntp_service_sync_cb(struct timeval *tv)
{
    (void)tv;
    time_synced = true;
}

esp_err_t sntp_service_init(void)
{
    esp_err_t ret;

    if (!sntp_started)
    {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
        config.sync_cb = sntp_service_sync_cb;

        ret = esp_netif_sntp_init(&config);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        sntp_started = true;
    }

    ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_SYNC_TIMEOUT_MS));
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SNTP sync failed: %s", esp_err_to_name(ret));
        return ret;
    }

    time_synced = true;
    time_t now = 0;
    struct tm timeinfo = {0};
    char time_buf[32];
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "System time synced: %s", time_buf);

    return ESP_OK;
}

bool sntp_service_time_synced(void)
{
    return time_synced;
}

esp_err_t sntp_service_get_time(time_t *now)
{
    if (now == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!time_synced)
    {
        return ESP_ERR_INVALID_STATE;
    }

    time(now);
    return ESP_OK;
}
