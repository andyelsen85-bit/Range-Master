#pragma once
#include "esp_http_client.h"
// One serialized persistent handle, shared by health, nonce, fire and events.
esp_http_client_handle_t gateway_http_acquire(const esp_http_client_config_t *request);
void gateway_http_release(esp_http_client_handle_t client,esp_err_t result);
