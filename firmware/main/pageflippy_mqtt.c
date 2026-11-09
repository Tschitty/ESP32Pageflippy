// ESP32Pageflippy: MQTT mit Home-Assistant-Discovery.
//
// Ist ein Broker eingetragen und das Heim-WLAN verbunden, meldet sich Pageflippy bei Home Assistant
// mit Knoepfen (vor, zurueck, Controller koppeln) und Sensoren (Tolino verbunden, Controller,
// letzte Eingabe, umgeblaetterte Seiten). Topics:
//   pageflippy/<id>/status   online / offline (Last Will)
//   pageflippy/<id>/state    JSON mit dem Status
//   pageflippy/<id>/cmd      "next", "prev" oder "pair"

#include "pageflippy_mqtt.h"

#include <stdio.h>
#include <string.h>

#include <cJSON.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>
#include <nvs.h>
#include <uni.h>

#include "pageflippy_lang.h"
#include "pageflippy_status.h"

#define NVS_NAMESPACE "pageflippy"
#define STATE_INTERVAL_MS 2000

static esp_mqtt_client_handle_t client;
static volatile bool connected;
static char dev_id[13];
static char base[32];

void pageflippy_mqtt_config_load(pageflippy_mqtt_config_t* out) {
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
        return;
    uint8_t en = 0;
    nvs_get_u8(h, "mqtt_en", &en);
    out->enabled = en;
    size_t len = sizeof(out->uri);
    nvs_get_str(h, "mqtt_uri", out->uri, &len);
    len = sizeof(out->user);
    nvs_get_str(h, "mqtt_user", out->user, &len);
    len = sizeof(out->pass);
    nvs_get_str(h, "mqtt_pass", out->pass, &len);
    nvs_close(h);
}

void pageflippy_mqtt_config_save(const pageflippy_mqtt_config_t* cfg) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u8(h, "mqtt_en", cfg->enabled);
    nvs_set_str(h, "mqtt_uri", cfg->uri);
    nvs_set_str(h, "mqtt_user", cfg->user);
    nvs_set_str(h, "mqtt_pass", cfg->pass);
    nvs_commit(h);
    nvs_close(h);
}

bool pageflippy_mqtt_connected(void) {
    return connected;
}

static void make_ids(void) {
    if (base[0])
        return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(dev_id, sizeof(dev_id), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(base, sizeof(base), "pageflippy/%s", dev_id);
}

const char* pageflippy_mqtt_base_topic(void) {
    make_ids();
    return base;
}

static void publish(const char* topic, const char* payload, bool retain) {
    esp_mqtt_client_publish(client, topic, payload, 0, 1, retain);
}

// Eine Discovery-Nachricht. "extra" wird als JSON-Objekt eingemischt.
static void announce(const char* component, const char* key, const char* name, const char* icon, cJSON* extra) {
    char topic[96];
    snprintf(topic, sizeof(topic), "homeassistant/%s/pageflippy_%s/%s/config", component, dev_id, key);

    cJSON* root = extra ? extra : cJSON_CreateObject();
    char buf[64];
    cJSON_AddStringToObject(root, "name", name);
    snprintf(buf, sizeof(buf), "pageflippy_%s_%s", dev_id, key);
    cJSON_AddStringToObject(root, "unique_id", buf);
    snprintf(buf, sizeof(buf), "%s/status", base);
    cJSON_AddStringToObject(root, "availability_topic", buf);
    if (icon)
        cJSON_AddStringToObject(root, "icon", icon);

    cJSON* dev = cJSON_AddObjectToObject(root, "device");
    cJSON* ids = cJSON_AddArrayToObject(dev, "identifiers");
    snprintf(buf, sizeof(buf), "pageflippy_%s", dev_id);
    cJSON_AddItemToArray(ids, cJSON_CreateString(buf));
    cJSON_AddStringToObject(dev, "name", "ESP32Pageflippy");
    cJSON_AddStringToObject(dev, "model", T("Tolino-Fernbedienung", "Tolino remote"));
    cJSON_AddStringToObject(dev, "manufacturer", "ESP32Pageflippy");

    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text) {
        publish(topic, text, true);
        free(text);
    }
}

static void announce_button(const char* key, const char* name, const char* icon, const char* payload) {
    cJSON* x = cJSON_CreateObject();
    char buf[48];
    snprintf(buf, sizeof(buf), "%s/cmd", base);
    cJSON_AddStringToObject(x, "command_topic", buf);
    cJSON_AddStringToObject(x, "payload_press", payload);
    announce("button", key, name, icon, x);
}

static void announce_state(const char* component, const char* key, const char* name, const char* icon,
                           const char* tmpl) {
    cJSON* x = cJSON_CreateObject();
    char buf[48];
    snprintf(buf, sizeof(buf), "%s/state", base);
    cJSON_AddStringToObject(x, "state_topic", buf);
    cJSON_AddStringToObject(x, "value_template", tmpl);
    if (strcmp(component, "binary_sensor") == 0) {
        cJSON_AddStringToObject(x, "payload_on", "ON");
        cJSON_AddStringToObject(x, "payload_off", "OFF");
        cJSON_AddStringToObject(x, "device_class", "connectivity");
    }
    announce(component, key, name, icon, x);
}

static void on_connected(void) {
    char topic[48];
    snprintf(topic, sizeof(topic), "%s/status", base);
    publish(topic, "online", true);
    snprintf(topic, sizeof(topic), "%s/cmd", base);
    esp_mqtt_client_subscribe(client, topic, 1);

    announce_button("next", T("Vor", "Next page"), "mdi:page-next", "next");
    announce_button("prev", T("Zurueck", "Previous page"), "mdi:page-previous", "prev");
    announce_button("pair", T("Controller koppeln", "Pair controller"), "mdi:bluetooth-connect", "pair");
    announce_state("binary_sensor", "tolino", T("Tolino verbunden", "Tolino connected"), "mdi:book-open-variant",
                   "{{ 'ON' if value_json.tolino else 'OFF' }}");
    announce_state("sensor", "controller", "Controller", "mdi:controller", "{{ value_json.controller }}");
    announce_state("sensor", "last_input", T("Letzte Eingabe", "Last input"), "mdi:gesture-tap-button",
                   "{{ value_json.last_input }}");
    announce_state("sensor", "flips", T("Umgeblaettert", "Pages turned"), "mdi:counter", "{{ value_json.flips }}");
    logi(T("pageflippy: MQTT verbunden, Home Assistant angemeldet\n",
           "pageflippy: MQTT connected, announced to Home Assistant\n"));
}

static void on_data(esp_mqtt_event_handle_t ev) {
    char data[16] = {0};
    int len = ev->data_len < (int)sizeof(data) - 1 ? ev->data_len : (int)sizeof(data) - 1;
    memcpy(data, ev->data, len);
    if (strcmp(data, "next") == 0)
        pageflippy_request_flip(true);
    else if (strcmp(data, "prev") == 0)
        pageflippy_request_flip(false);
    else if (strcmp(data, "pair") == 0)
        pageflippy_request_pairing();
}

static void mqtt_event(void* arg, esp_event_base_t event_base, int32_t id, void* data) {
    ARG_UNUSED(arg);
    ARG_UNUSED(event_base);
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            on_connected();
            break;
        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            break;
        case MQTT_EVENT_DATA:
            on_data(ev);
            break;
        default:
            break;
    }
}

// Status an Home Assistant schicken, wenn er sich geaendert hat.
static void state_task(void* arg) {
    ARG_UNUSED(arg);
    char last[256] = {0};
    char topic[48];
    snprintf(topic, sizeof(topic), "%s/state", base);
    while (true) {
        if (connected) {
            pageflippy_status_t st;
            pageflippy_status_get(&st);
            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "tolino", st.tolino_connected);
            cJSON_AddStringToObject(root, "controller",
                                    st.controller_count ? st.controllers[0] : T("keiner", "none"));
            cJSON_AddStringToObject(root, "last_input", st.last_input[0] ? st.last_input : "-");
            cJSON_AddNumberToObject(root, "flips", st.flips);
            char* text = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (text && strcmp(text, last) != 0) {
                publish(topic, text, true);
                snprintf(last, sizeof(last), "%s", text);
            }
            free(text);
        } else {
            last[0] = 0;  // nach dem Wiederverbinden alles neu senden
        }
        vTaskDelay(pdMS_TO_TICKS(STATE_INTERVAL_MS));
    }
}

// Laeuft in einem eigenen Task: der WLAN-Event-Task (sys_evt) hat zu wenig Stack fuer den MQTT-Client.
static void start_task(void* arg);

void pageflippy_mqtt_start(void) {
    static bool started;
    if (started)
        return;
    started = true;
    xTaskCreate(start_task, "mqtt_start", 4096, NULL, 3, NULL);
}

static void start_task(void* arg) {
    ARG_UNUSED(arg);
    static pageflippy_mqtt_config_t cfg;
    pageflippy_mqtt_config_load(&cfg);
    if (!cfg.enabled || cfg.uri[0] == 0) {
        vTaskDelete(NULL);
        return;
    }

    make_ids();
    static char lwt_topic[48];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", base);

    esp_mqtt_client_config_t mc = {
        .broker.address.uri = cfg.uri,
        .credentials.username = cfg.user[0] ? cfg.user : NULL,
        .credentials.authentication.password = cfg.pass[0] ? cfg.pass : NULL,
        .session.last_will = {.topic = lwt_topic, .msg = "offline", .qos = 1, .retain = 1},
        .task.stack_size = 6144,
    };
    client = esp_mqtt_client_init(&mc);
    if (client == NULL) {
        loge(T("pageflippy: MQTT-Adresse ungueltig: %s\n", "pageflippy: invalid MQTT address: %s\n"), cfg.uri);
        vTaskDelete(NULL);
        return;
    }
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_mqtt_client_start(client);
    xTaskCreate(state_task, "mqtt_state", 4096, NULL, 2, NULL);
    logi(T("pageflippy: MQTT verbindet mit %s\n", "pageflippy: MQTT connecting to %s\n"), cfg.uri);
    vTaskDelete(NULL);
}
