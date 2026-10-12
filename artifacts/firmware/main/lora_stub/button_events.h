#pragma once
#include <stddef.h>
#include <stdint.h>
// Only the LVGL task publishes settings and consumes commands. Network code
// receives an immutable copy and never accesses g_store or LVGL.
struct ButtonEventsConfig {
    bool enabled,gameActive;
    char url[256],key[256];
};
struct RemoteButtonEvent { uint8_t src,btn; uint32_t receivedMs,generation; };
void button_events_init();
void button_events_publish(const ButtonEventsConfig &config);
bool button_events_take(RemoteButtonEvent *out);
void button_events_status(bool *connected,uint32_t *ageMs);
