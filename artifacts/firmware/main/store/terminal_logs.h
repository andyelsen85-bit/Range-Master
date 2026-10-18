#pragma once
#include <stddef.h>
#include "cJSON.h"
#include <cstring>
#include <initializer_list>
inline const char *terminal_log_wire_type(const char *type) {
    for(const char *known:{"fire_trace","gateway_trace","sync","data_loss_risk","ui_commit","wifi"})
        if(type&&!strcmp(type,known))return known;
    return "sync";
}

// Diagnostic data is separate from game/sales outboxes, FAT caches and backups.
struct TerminalLogEntry {
    char id[64], ts[32], type[20], level[6], msg[201], data[513];
};
void terminal_logs_init();
void terminal_log_add(const char *type, const char *level, const char *message,
                      const cJSON *data = nullptr);
int terminal_logs_snapshot(TerminalLogEntry *out, int max);
void terminal_logs_ack(const char *const *ids, int count);
void terminal_logs_flush_critical();
