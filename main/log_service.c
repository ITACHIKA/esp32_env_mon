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
#include "freertos/task.h"

static const char *TAG = "LOGSVC";

#define LOG_SERVICE_PRINT_BUF_SIZE 128

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

static esp_err_t log_service_find_tail_start(const char *path, size_t line_count, long *start_offset, size_t *lines_found)
{
    *start_offset = 0;
    *lines_found = 0;

    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        return errno == ENOENT ? ESP_OK : ESP_FAIL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return ESP_FAIL;
    }

    long block_end = ftell(file);
    if (block_end < 0)
    {
        fclose(file);
        return ESP_FAIL;
    }
    if (block_end == 0)
    {
        fclose(file);
        return ESP_OK;
    }

    char buf[LOG_SERVICE_PRINT_BUF_SIZE];
    size_t newline_count = 0;
    bool saw_content = false;
    bool found = false;

    while (block_end > 0 && !found)
    {
        size_t read_size = block_end > (long)sizeof(buf) ? sizeof(buf) : (size_t)block_end;
        long block_start = block_end - (long)read_size;

        if (fseek(file, block_start, SEEK_SET) != 0)
        {
            fclose(file);
            return ESP_FAIL;
        }

        size_t bytes_read = fread(buf, 1, read_size, file);
        if (bytes_read != read_size && ferror(file))
        {
            fclose(file);
            return ESP_FAIL;
        }

        for (size_t i = bytes_read; i > 0; --i)
        {
            long char_pos = block_start + (long)i - 1;
            char ch = buf[i - 1];

            if (!saw_content && ch == '\n')
            {
                continue;
            }

            saw_content = true;
            if (ch == '\n')
            {
                newline_count++;
                if (newline_count == line_count)
                {
                    *start_offset = char_pos + 1;
                    *lines_found = line_count;
                    found = true;
                    break;
                }
            }
        }

        block_end = block_start;
        vTaskDelay(1);
    }

    fclose(file);

    if (!found && saw_content)
    {
        *start_offset = 0;
        *lines_found = newline_count + 1;
    }

    return ESP_OK;
}

static esp_err_t log_service_print_file_from_offset(const char *path, long offset)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        return errno == ENOENT ? ESP_OK : ESP_FAIL;
    }

    if (fseek(file, offset, SEEK_SET) != 0)
    {
        fclose(file);
        return ESP_FAIL;
    }

    char line_buf[LOG_SERVICE_PRINT_BUF_SIZE];
    size_t line_buf_len = 0;
    size_t read_count = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF)
    {
        if (ch == '\n')
        {
            log_service_print_buffer(line_buf, &line_buf_len);
        }
        else
        {
            line_buf[line_buf_len++] = (char)ch;
            if (line_buf_len == sizeof(line_buf) - 1)
            {
                log_service_print_buffer(line_buf, &line_buf_len);
            }
        }

        read_count++;
        if ((read_count % 512) == 0)
        {
            vTaskDelay(1);
        }
    }

    esp_err_t ret = ferror(file) ? ESP_FAIL : ESP_OK;
    fclose(file);

    if (ret == ESP_OK)
    {
        log_service_print_buffer(line_buf, &line_buf_len);
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

    esp_err_t err = ESP_OK;
    if (line_count == 0)
    {
        err = log_service_print_file_from_offset(LOG_SERVICE_BACKUP_FILE_PATH, 0);
        if (err == ESP_OK)
        {
            err = log_service_print_file_from_offset(LOG_SERVICE_FILE_PATH, 0);
        }
    }
    else
    {
        size_t requested_lines = (size_t)line_count;
        long current_start = 0;
        size_t current_lines = 0;
        err = log_service_find_tail_start(LOG_SERVICE_FILE_PATH, requested_lines, &current_start, &current_lines);

        if (err == ESP_OK && current_lines >= requested_lines)
        {
            err = log_service_print_file_from_offset(LOG_SERVICE_FILE_PATH, current_start);
        }
        else if (err == ESP_OK)
        {
            long backup_start = 0;
            size_t backup_lines = 0;
            size_t remaining_lines = requested_lines - current_lines;
            err = log_service_find_tail_start(LOG_SERVICE_BACKUP_FILE_PATH, remaining_lines, &backup_start, &backup_lines);
            if (err == ESP_OK)
            {
                err = log_service_print_file_from_offset(LOG_SERVICE_BACKUP_FILE_PATH, backup_start);
            }
            if (err == ESP_OK)
            {
                err = log_service_print_file_from_offset(LOG_SERVICE_FILE_PATH, 0);
            }
        }
    }

    xSemaphoreGive(log_mutex);
    return err;
}
