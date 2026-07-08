#include "log_service.h"

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "LOGSVC";

static SemaphoreHandle_t log_mutex;
static bool log_mounted = false;

static esp_err_t log_service_remove_if_exists(const char *path)
{
    if (remove(path) != 0 && errno != ENOENT)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t log_service_rotate(void)
{
    struct stat st;
    if (stat(LOG_SERVICE_FILE_PATH, &st) != 0)
    {
        return ESP_OK;
    }

    if (log_service_remove_if_exists(LOG_SERVICE_BACKUP_FILE_PATH) != ESP_OK)
    {
        ESP_LOGE(TAG, "Remove old backup log failed");
        return ESP_FAIL;
    }

    if (rename(LOG_SERVICE_FILE_PATH, LOG_SERVICE_BACKUP_FILE_PATH) != 0)
    {
        ESP_LOGE(TAG, "Rotate log failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t log_service_count_file_lines(const char *path, size_t *line_count)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        return errno == ENOENT ? ESP_OK : ESP_FAIL;
    }

    bool has_data = false;
    bool last_was_newline = true;
    int ch;
    while ((ch = fgetc(file)) != EOF)
    {
        has_data = true;
        if (ch == '\n')
        {
            (*line_count)++;
            last_was_newline = true;
        }
        else
        {
            last_was_newline = false;
        }
    }

    esp_err_t ret = ferror(file) ? ESP_FAIL : ESP_OK;
    fclose(file);

    if (ret == ESP_OK && has_data && !last_was_newline)
    {
        (*line_count)++;
    }

    return ret;
}

static void log_service_print_buffer(char *buf, size_t *buf_len)
{
    if (*buf_len == 0)
    {
        return;
    }

    buf[*buf_len] = '\0';
    ESP_LOGI(TAG, "%s", buf);
    *buf_len = 0;
}

static esp_err_t log_service_print_file_from_line(const char *path, size_t start_line, size_t *current_line)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        return errno == ENOENT ? ESP_OK : ESP_FAIL;
    }

    char line_buf[128];
    size_t line_buf_len = 0;
    bool has_data = false;
    bool last_was_newline = true;
    int ch;
    while ((ch = fgetc(file)) != EOF)
    {
        has_data = true;
        if (ch == '\n')
        {
            if (*current_line >= start_line)
            {
                log_service_print_buffer(line_buf, &line_buf_len);
            }
            (*current_line)++;
            last_was_newline = true;
            continue;
        }

        last_was_newline = false;
        if (*current_line >= start_line)
        {
            line_buf[line_buf_len++] = (char)ch;
            if (line_buf_len == sizeof(line_buf) - 1)
            {
                log_service_print_buffer(line_buf, &line_buf_len);
            }
        }
    }

    esp_err_t ret = ferror(file) ? ESP_FAIL : ESP_OK;
    fclose(file);

    if (ret == ESP_OK && has_data && !last_was_newline)
    {
        if (*current_line >= start_line)
        {
            log_service_print_buffer(line_buf, &line_buf_len);
        }
        (*current_line)++;
    }

    return ret;
}

esp_err_t log_service_init(void)
{
    if (log_mounted)
    {
        return ESP_OK;
    }

    if (log_mutex == NULL)
    {
        log_mutex = xSemaphoreCreateMutex();
        if (log_mutex == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = LOG_SERVICE_BASE_PATH,
        .partition_label = "logs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0;
    size_t used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LittleFS info failed: %s", esp_err_to_name(ret));
        esp_vfs_littlefs_unregister(conf.partition_label);
        return ret;
    }

    log_mounted = true;
    ESP_LOGI(TAG, "LittleFS mounted, total: %u, used: %u", (unsigned int)total, (unsigned int)used);
    return ESP_OK;
}

esp_err_t log_service_write(const char *log)
{
    if (!log_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (log == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(log_mutex, portMAX_DELAY);

    struct stat st;
    size_t len = strlen(log);
    size_t write_len = len + ((len == 0 || log[len - 1] != '\n') ? 1 : 0);
    if (stat(LOG_SERVICE_FILE_PATH, &st) == 0 &&
        st.st_size + write_len > LOG_SERVICE_MAX_FILE_SIZE)
    {
        esp_err_t rotate_ret = log_service_rotate();
        if (rotate_ret != ESP_OK)
        {
            xSemaphoreGive(log_mutex);
            return rotate_ret;
        }
    }

    FILE *file = fopen(LOG_SERVICE_FILE_PATH, "a");
    if (file == NULL)
    {
        xSemaphoreGive(log_mutex);
        return ESP_FAIL;
    }

    int ret = fputs(log, file);
    if (ret >= 0 && (len == 0 || log[len - 1] != '\n'))
    {
        ret = fputc('\n', file);
    }

    esp_err_t err = ESP_OK;
    if (ret < 0 || fflush(file) != 0 || fsync(fileno(file)) != 0)
    {
        err = ESP_FAIL;
    }

    fclose(file);
    xSemaphoreGive(log_mutex);
    return err;
}

esp_err_t log_service_clear(void)
{
    if (!log_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(log_mutex, portMAX_DELAY);

    esp_err_t err = log_service_remove_if_exists(LOG_SERVICE_FILE_PATH);
    if (err == ESP_OK)
    {
        err = log_service_remove_if_exists(LOG_SERVICE_BACKUP_FILE_PATH);
    }

    xSemaphoreGive(log_mutex);
    return err;
}

esp_err_t log_service_print(int line_count)
{
    if (!log_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (line_count < 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(log_mutex, portMAX_DELAY);

    size_t total_lines = 0;
    esp_err_t err = log_service_count_file_lines(LOG_SERVICE_BACKUP_FILE_PATH, &total_lines);
    if (err == ESP_OK)
    {
        err = log_service_count_file_lines(LOG_SERVICE_FILE_PATH, &total_lines);
    }

    if (err == ESP_OK)
    {
        size_t print_lines = line_count == 0 ? total_lines : (size_t)line_count;
        size_t start_line = total_lines > print_lines ? total_lines - print_lines : 0;
        size_t current_line = 0;
        err = log_service_print_file_from_line(LOG_SERVICE_BACKUP_FILE_PATH, start_line, &current_line);
        if (err == ESP_OK)
        {
            err = log_service_print_file_from_line(LOG_SERVICE_FILE_PATH, start_line, &current_line);
        }
    }

    xSemaphoreGive(log_mutex);
    return err;
}
