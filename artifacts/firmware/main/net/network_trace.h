#pragma once
#include <stdint.h>
#include "esp_err.h"
void network_trace_begin(const char *url,const char *path,unsigned retry,int64_t deadline=0);
void network_trace_end(esp_err_t result,int status);
void network_trace_install_log_filter();
int network_rssi();
int network_disconnect_reason();
