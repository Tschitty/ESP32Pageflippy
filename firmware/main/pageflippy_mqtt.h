// ESP32Pageflippy: MQTT mit Home-Assistant-Discovery.
#pragma once

#include <stdbool.h>

typedef struct {
    bool enabled;
    char uri[96];   // z. B. mqtt://192.168.1.10:1883
    char user[64];
    char pass[64];
} pageflippy_mqtt_config_t;

void pageflippy_mqtt_config_load(pageflippy_mqtt_config_t* out);
void pageflippy_mqtt_config_save(const pageflippy_mqtt_config_t* cfg);

// Startet den Client (wenn aktiviert). Aufruf, sobald das Heim-WLAN eine IP hat.
void pageflippy_mqtt_start(void);
bool pageflippy_mqtt_connected(void);
// Basis-Topic, z. B. "pageflippy/a1b2c3"
const char* pageflippy_mqtt_base_topic(void);
