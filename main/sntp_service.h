#pragma once
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

esp_err_t sntp_service_init(void);
bool sntp_service_time_synced(void);
esp_err_t sntp_service_get_time(time_t *now);
