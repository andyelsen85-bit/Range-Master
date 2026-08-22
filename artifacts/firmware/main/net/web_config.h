#pragma once
// ============================================================
// web_config — tiny HTTP config server
//
// Serves a single-page form at http://<terminal-ip>/
// so operators can set the API URL/key and gateway URL/key from any
// browser on the same network, without using the on-screen
// keyboard.  Starts automatically when WiFi gets an IP;
// stops on disconnect.
// ============================================================
#ifdef __cplusplus
extern "C" {
#endif

void web_config_start(void);
void web_config_stop(void);

#ifdef __cplusplus
}
#endif
