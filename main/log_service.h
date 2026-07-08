#pragma once
<<<<<<< HEAD
#include <stddef.h>
=======
>>>>>>> 50e8f2d7dbc753499fa4925cb48d3e542429cb79
#include "esp_err.h"

#define LOG_SERVICE_BASE_PATH "/littlefs"
#define LOG_SERVICE_FILE_PATH LOG_SERVICE_BASE_PATH "/system.log"
#define LOG_SERVICE_BACKUP_FILE_PATH LOG_SERVICE_BASE_PATH "/system.log.1"
#define LOG_SERVICE_MAX_FILE_SIZE (448 * 1024)

esp_err_t log_service_init(void);
esp_err_t log_service_write(const char *log);
esp_err_t log_service_clear(void);
<<<<<<< HEAD
esp_err_t log_service_print(int line_count);
=======
esp_err_t log_service_print(size_t line_count);
>>>>>>> 50e8f2d7dbc753499fa4925cb48d3e542429cb79
