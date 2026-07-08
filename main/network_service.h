#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t networkInit(void);

extern bool networkReady;
