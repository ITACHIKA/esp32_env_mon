#include "esp_err.h"

esp_err_t mqtt_init();
esp_err_t mqtt_publish(const char* topic,const char* msg);