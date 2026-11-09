// ESP32Pageflippy: WLAN (Heimnetz und/oder Hotspot) und Weboberflaeche.
//
// - Ohne Heim-WLAN: nur der Hotspot "ESP32Pageflippy", Passwort "pageflippy", http://192.168.4.1
// - Mit Heim-WLAN: der ESP32 verbindet sich damit, der Hotspot bleibt aus. Ist das Heim-WLAN
//   5 Minuten lang nicht erreichbar, geht der Hotspot an. Im Hintergrund wird weiter versucht;
//   steht die Verbindung wieder und ist niemand im Hotspot, geht er wieder aus.
// - Ist 5 Minuten lang niemand im Hotspot (und kein Heim-WLAN verbunden), schaltet sich das WLAN
//   ganz ab bis zum Neustart. So stoert es Bluetooth nicht und es gibt kein verwaistes WLAN.
// - BOOT-Taste 3 Sekunden halten: WLAN-Hotspot bis zum Neustart an (auch wenn das WLAN aus war).

#include "pageflippy_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <argtable3/argtable3.h>
#include <cJSON.h>
#include <esp_console.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <uni.h>

#include "pageflippy_config.h"
#include "pageflippy_lang.h"
#include "pageflippy_status.h"
#include "pageflippy_touch.h"
#include "pageflippy_mqtt.h"
#include "pageflippy_update.h"

#define AP_SSID "ESP32Pageflippy"
#define STA_FALLBACK_MS (5 * 60 * 1000)  // so lange ohne Heim-WLAN, dann Hotspot an
#define AP_IDLE_OFF_MS (5 * 60 * 1000)   // so lange niemand im Hotspot, dann WLAN aus
#define STA_RETRY_MS (30 * 1000)       // bei Hotspot an: so oft neu versuchen
#define SUPERVISOR_US (5 * 1000 * 1000)

#ifdef PAGEFLIPPY_LANG_EN
extern const char index_html_start[] asm("_binary_index_en_html_gz_start");
extern const char index_html_end[] asm("_binary_index_en_html_gz_end");
#else
extern const char index_html_start[] asm("_binary_index_de_html_gz_start");
extern const char index_html_end[] asm("_binary_index_de_html_gz_end");
#endif

static pageflippy_wifi_config_t wifi_cfg;
static wifi_config_t ap_config;
static char sta_ip[16];
static volatile bool ap_on;
static volatile bool ap_forced;
static volatile bool sta_connecting;
static volatile bool scanning;
static volatile bool wifi_off;
static int64_t ap_used_ms;      // zuletzt jemand im Hotspot (bzw. Hotspot eingeschaltet)
static int64_t sta_lost_ms;     // seit wann ohne Heim-WLAN
static int64_t sta_attempt_ms;  // letzter Verbindungsversuch

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static bool has_sta(void) {
    return wifi_cfg.sta_ssid[0] != 0;
}

//
// WLAN
//
static void set_ap(bool on) {
    if (on == ap_on)
        return;
    ap_on = on;
    esp_wifi_set_mode(on ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (on) {
        ap_used_ms = now_ms();
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        logi(T("pageflippy: Hotspot \"%s\" an, Weboberflaeche unter http://192.168.4.1\n",
               "pageflippy: hotspot \"%s\" on, web interface at http://192.168.4.1\n"),
             AP_SSID);
    } else {
        logi(T("pageflippy: Hotspot aus (Heim-WLAN verbunden)\n", "pageflippy: hotspot off (home Wi-Fi connected)\n"));
    }
}

static void sta_connect(void) {
    if (!has_sta() || sta_connecting || scanning || wifi_off)
        return;
    sta_connecting = true;
    sta_attempt_ms = now_ms();
    esp_wifi_connect();
}

static int ap_client_count(void) {
    wifi_sta_list_t list;
    if (!ap_on || esp_wifi_ap_get_sta_list(&list) != ESP_OK)
        return 0;
    return list.num;
}

// Laeuft alle 5 Sekunden: Hotspot an/aus, neue Verbindungsversuche.
static void wifi_supervisor(void* arg) {
    ARG_UNUSED(arg);
    if (wifi_off)
        return;
    int64_t now = now_ms();

    // Hotspot ungenutzt: WLAN ganz aus bis zum Neustart (BOOT-Taste lang holt den Hotspot zurueck)
    if (ap_on && sta_ip[0] == 0) {
        if (ap_client_count() > 0)
            ap_used_ms = now;
        else if (!ap_forced && now - ap_used_ms >= AP_IDLE_OFF_MS) {
            logi(T("pageflippy: Hotspot %d Minuten ungenutzt, WLAN aus bis zum Neustart (BOOT 3 s = Hotspot an)\n",
                   "pageflippy: hotspot unused for %d minutes, Wi-Fi off until restart (BOOT 3 s = hotspot on)\n"),
                 AP_IDLE_OFF_MS / 60000);
            wifi_off = true;
            ap_on = false;
            sta_connecting = false;
            esp_wifi_stop();
            return;
        }
    }
    if (!has_sta())
        return;
    if (sta_ip[0]) {
        // Heim-WLAN steht: Hotspot aus, sobald niemand mehr darin ist
        if (ap_on && !ap_forced && ap_client_count() == 0)
            set_ap(false);
        return;
    }
    if (!ap_on && now - sta_lost_ms >= STA_FALLBACK_MS) {
        logi(T("pageflippy: Heim-WLAN \"%s\" seit %d Minuten nicht erreichbar\n",
               "pageflippy: home Wi-Fi \"%s\" not reachable for %d minutes\n"),
             wifi_cfg.sta_ssid, STA_FALLBACK_MS / 60000);
        set_ap(true);
    }
    // Mit Hotspot seltener versuchen: jeder Versuch stoert die Hotspot-Verbindung kurz.
    if (!sta_connecting && now - sta_attempt_ms >= (ap_on ? STA_RETRY_MS : 2000))
        sta_connect();
}

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    ARG_UNUSED(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        sta_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (sta_ip[0])
            sta_lost_ms = now_ms();
        sta_ip[0] = 0;
        sta_connecting = false;
        // Ohne Hotspot sofort neu versuchen, sonst macht das der Supervisor.
        if (!ap_on)
            sta_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* ev = (ip_event_got_ip_t*)data;
        snprintf(sta_ip, sizeof(sta_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        sta_connecting = false;
        logi(T("pageflippy: Heim-WLAN verbunden, Weboberflaeche unter http://%s\n",
               "pageflippy: home Wi-Fi connected, web interface at http://%s\n"),
             sta_ip);
        pageflippy_mqtt_start();
    }
}

static void wifi_start(void) {
    pageflippy_wifi_config_load(&wifi_cfg);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    // STA immer anlegen: wird auch fuer die WLAN-Suche gebraucht.
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
    // Name im Router / DHCP (statt "espressif")
    esp_netif_set_hostname(sta_netif, AP_SSID);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    strcpy((char*)ap_config.ap.ssid, AP_SSID);
    ap_config.ap.ssid_len = strlen(AP_SSID);
    strncpy((char*)ap_config.ap.password, wifi_cfg.ap_pass, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.channel = 6;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    if (has_sta()) {
        // Heim-WLAN zuerst, Hotspot nur als Rueckfallebene
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_config_t sta = {0};
        strncpy((char*)sta.sta.ssid, wifi_cfg.sta_ssid, sizeof(sta.sta.ssid) - 1);
        strncpy((char*)sta.sta.password, wifi_cfg.sta_pass, sizeof(sta.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        logi(T("pageflippy: verbinde mit Heim-WLAN \"%s\"\n", "pageflippy: connecting to home Wi-Fi \"%s\"\n"),
             wifi_cfg.sta_ssid);
    } else {
        // Nur Hotspot; STA bleibt fuer die WLAN-Suche aktiv
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ap_on = true;
        ap_used_ms = now_ms();
        logi(T("pageflippy: Hotspot \"%s\" an, Weboberflaeche unter http://192.168.4.1\n",
               "pageflippy: hotspot \"%s\" on, web interface at http://192.168.4.1\n"),
             AP_SSID);
    }
    sta_lost_ms = now_ms();
    ESP_ERROR_CHECK(esp_wifi_start());
    // Funk-Stromsparen aus: sonst reagiert die Seite traege. Bluetooth teilt sich die Antenne.
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    static esp_timer_handle_t supervisor;
    const esp_timer_create_args_t args = {.callback = wifi_supervisor, .name = "wifi_sup"};
    esp_timer_create(&args, &supervisor);
    esp_timer_start_periodic(supervisor, SUPERVISOR_US);
}

// BOOT-Taste lang: Hotspot bis zum Neustart erzwingen
static void force_ap(void) {
    ap_forced = true;
    if (wifi_off) {
        // WLAN war ganz aus: mit Hotspot neu starten
        wifi_off = false;
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        esp_wifi_start();
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        ap_on = true;
        logi(T("pageflippy: Hotspot \"%s\" an, Weboberflaeche unter http://192.168.4.1\n",
               "pageflippy: hotspot \"%s\" on, web interface at http://192.168.4.1\n"),
             AP_SSID);
        return;
    }
    set_ap(true);
}

//
// HTTP-Hilfen
//
static esp_err_t send_json(httpd_req_t* req, cJSON* root) {
    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL)
        return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}

static cJSON* read_json(httpd_req_t* req) {
    if (req->content_len == 0 || req->content_len > 4096)
        return NULL;
    char* buf = malloc(req->content_len + 1);
    if (buf == NULL)
        return NULL;
    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        received += r;
    }
    buf[received] = 0;
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    return root;
}

static esp_err_t send_ok(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_json(req, root);
}

static cJSON* mask_to_json(uint32_t mask) {
    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < INPUT_COUNT; i++) {
        if (mask & BIT(i))
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(i));
    }
    return arr;
}

static uint32_t json_to_mask(const cJSON* arr) {
    uint32_t mask = 0;
    const cJSON* item;
    cJSON_ArrayForEach(item, arr) {
        if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint < INPUT_COUNT)
            mask |= BIT(item->valueint);
    }
    return mask;
}

static cJSON* keys_to_json(const uint8_t* keys) {
    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < PAGEFLIPPY_MAX_KEYS; i++) {
        if (keys[i] != 0)
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(keys[i]));
    }
    return arr;
}

static void json_to_keys(const cJSON* arr, uint8_t* keys) {
    memset(keys, 0, PAGEFLIPPY_MAX_KEYS);
    int n = 0;
    const cJSON* item;
    cJSON_ArrayForEach(item, arr) {
        if (n < PAGEFLIPPY_MAX_KEYS && cJSON_IsNumber(item) && item->valueint > 0 && item->valueint < 256)
            keys[n++] = item->valueint;
    }
}

//
// Seiten und API
//
static esp_err_t index_get(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

static esp_err_t config_get(httpd_req_t* req) {
    const pageflippy_config_t* cfg = &pageflippy_config;
    cJSON* root = cJSON_CreateObject();

    cJSON* inputs = cJSON_AddArrayToObject(root, "inputs");
    for (int i = 0; i < INPUT_COUNT; i++)
        cJSON_AddItemToArray(inputs, cJSON_CreateString(pageflippy_input_name((pageflippy_input_t)i)));

    cJSON_AddItemToObject(root, "next", mask_to_json(cfg->next_mask));
    cJSON_AddItemToObject(root, "prev", mask_to_json(cfg->prev_mask));
    cJSON_AddItemToObject(root, "next_keys", keys_to_json(cfg->next_keys));
    cJSON_AddItemToObject(root, "prev_keys", keys_to_json(cfg->prev_keys));
    cJSON_AddNumberToObject(root, "out_next", cfg->out_next);
    cJSON_AddNumberToObject(root, "out_prev", cfg->out_prev);
    cJSON_AddBoolToObject(root, "rumble", cfg->rumble);
    cJSON_AddNumberToObject(root, "idle_off_min", cfg->idle_off_s / 60.0);
    cJSON_AddBoolToObject(root, "touch", cfg->touch_enabled);

    cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "sta_ssid", wifi_cfg.sta_ssid);
    cJSON_AddStringToObject(wifi, "ap_ssid", AP_SSID);
    pageflippy_mqtt_config_t mq;
    pageflippy_mqtt_config_load(&mq);
    cJSON* mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddBoolToObject(mqtt, "enabled", mq.enabled);
    cJSON_AddStringToObject(mqtt, "uri", mq.uri);
    cJSON_AddStringToObject(mqtt, "user", mq.user);
    cJSON_AddStringToObject(mqtt, "topic", pageflippy_mqtt_base_topic());
    cJSON_AddStringToObject(root, "version", pageflippy_update_version());
    cJSON_AddStringToObject(root, "repo", pageflippy_update_repo());
    cJSON_AddStringToObject(root, "lang", T("DE", "EN"));
    return send_json(req, root);
}

// Uebernimmt die Belegung aus JSON (Felder wie bei GET /api/config). Fehlende Felder bleiben.
static void apply_mapping(const cJSON* root, pageflippy_config_t* cfg) {
    const cJSON* item;
    if ((item = cJSON_GetObjectItem(root, "next")) && cJSON_IsArray(item))
        cfg->next_mask = json_to_mask(item);
    if ((item = cJSON_GetObjectItem(root, "prev")) && cJSON_IsArray(item))
        cfg->prev_mask = json_to_mask(item);
    if ((item = cJSON_GetObjectItem(root, "next_keys")) && cJSON_IsArray(item))
        json_to_keys(item, cfg->next_keys);
    if ((item = cJSON_GetObjectItem(root, "prev_keys")) && cJSON_IsArray(item))
        json_to_keys(item, cfg->prev_keys);
    if ((item = cJSON_GetObjectItem(root, "out_next")) && cJSON_IsNumber(item) && item->valueint > 0 &&
        item->valueint < 256)
        cfg->out_next = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "out_prev")) && cJSON_IsNumber(item) && item->valueint > 0 &&
        item->valueint < 256)
        cfg->out_prev = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "rumble")) && cJSON_IsBool(item))
        cfg->rumble = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "touch")) && cJSON_IsBool(item))
        cfg->touch_enabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "idle_off_min")) && cJSON_IsNumber(item) && item->valuedouble >= 0 &&
        item->valuedouble <= 600)
        cfg->idle_off_s = (uint16_t)(item->valuedouble * 60);
}

static esp_err_t config_post(httpd_req_t* req) {
    cJSON* root = read_json(req);
    if (root == NULL)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, T("Ungueltige Daten", "Invalid data"));

    pageflippy_config_t cfg = pageflippy_config;
    apply_mapping(root, &cfg);
    cJSON_Delete(root);

    pageflippy_config = cfg;
    pageflippy_config_save();
    logi(T("pageflippy: Belegung gespeichert\n", "pageflippy: mapping saved\n"));
    return send_ok(req);
}

static esp_err_t status_get(httpd_req_t* req) {
    pageflippy_status_t st;
    pageflippy_status_get(&st);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "tolino", st.tolino_connected);
    cJSON* ctrls = cJSON_AddArrayToObject(root, "controllers");
    for (int i = 0; i < st.controller_count; i++)
        cJSON_AddItemToArray(ctrls, cJSON_CreateString(st.controllers[i]));
    cJSON_AddStringToObject(root, "last_input", st.last_input);
    cJSON_AddNumberToObject(root, "flips", st.flips);
    cJSON_AddNumberToObject(root, "pairing_left_s", st.pairing_left_s);
    cJSON_AddBoolToObject(root, "bt_off", st.bt_off);
    cJSON_AddNumberToObject(root, "config_rev", pageflippy_config_revision());
    uint16_t touch_next, touch_prev;
    pageflippy_touch_values(&touch_next, &touch_prev);
    cJSON_AddNumberToObject(root, "touch_next", touch_next);
    cJSON_AddNumberToObject(root, "touch_prev", touch_prev);
    cJSON_AddStringToObject(root, "sta_ip", sta_ip);
    cJSON_AddBoolToObject(root, "ap_on", ap_on);
    cJSON_AddBoolToObject(root, "mqtt", pageflippy_mqtt_connected());
    pageflippy_update_status_t up;
    pageflippy_update_get_status(&up);
    cJSON* upd = cJSON_AddObjectToObject(root, "update");
    cJSON_AddNumberToObject(upd, "state", up.state);
    cJSON_AddNumberToObject(upd, "pct", up.percent);
    cJSON_AddStringToObject(upd, "msg", up.message);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000);
    return send_json(req, root);
}

static esp_err_t wifi_post(httpd_req_t* req) {
    cJSON* root = read_json(req);
    if (root == NULL)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, T("Ungueltige Daten", "Invalid data"));

    pageflippy_wifi_config_t cfg = wifi_cfg;
    const cJSON* item;
    if ((item = cJSON_GetObjectItem(root, "sta_ssid")) && cJSON_IsString(item))
        snprintf(cfg.sta_ssid, sizeof(cfg.sta_ssid), "%s", item->valuestring);
    // Leeres Passwort heisst "unveraendert", ausser es wurde ein anderes Netz gewaehlt (z.B. offenes WLAN).
    bool new_net = strcmp(cfg.sta_ssid, wifi_cfg.sta_ssid) != 0;
    if ((item = cJSON_GetObjectItem(root, "sta_pass")) && cJSON_IsString(item) && (item->valuestring[0] || new_net))
        snprintf(cfg.sta_pass, sizeof(cfg.sta_pass), "%s", item->valuestring);
    if (cfg.sta_ssid[0] == 0)
        cfg.sta_pass[0] = 0;
    if ((item = cJSON_GetObjectItem(root, "ap_pass")) && cJSON_IsString(item) && strlen(item->valuestring) >= 8)
        snprintf(cfg.ap_pass, sizeof(cfg.ap_pass), "%s", item->valuestring);
    cJSON_Delete(root);

    pageflippy_wifi_config_save(&cfg);
    logi(T("pageflippy: WLAN-Einstellungen gespeichert, Neustart\n", "pageflippy: Wi-Fi settings saved, restarting\n"));
    send_ok(req);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// Sucht WLANs in der Umgebung. Dauert ca. 2 Sekunden.
static int ap_record_cmp(const void* a, const void* b) {
    return ((const wifi_ap_record_t*)b)->rssi - ((const wifi_ap_record_t*)a)->rssi;
}

static esp_err_t scan_get(httpd_req_t* req) {
    // Laeuft gerade ein Verbindungsversuch, stoert der die Suche: abbrechen, danach neu versuchen.
    scanning = true;
    if (sta_connecting) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    wifi_scan_config_t scan = {.show_hidden = false};
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    scanning = false;
    if (err != ESP_OK) {
        loge(T("pageflippy: WLAN-Suche fehlgeschlagen: %s\n", "pageflippy: Wi-Fi scan failed: %s\n"), esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, T("WLAN-Suche fehlgeschlagen", "Wi-Fi scan failed"));
    }

    uint16_t count = 30;
    wifi_ap_record_t* recs = calloc(count, sizeof(wifi_ap_record_t));
    if (recs == NULL) {
        esp_wifi_clear_ap_list();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, T("Kein Speicher", "Out of memory"));
    }
    esp_wifi_scan_get_ap_records(&count, recs);
    qsort(recs, count, sizeof(wifi_ap_record_t), ap_record_cmp);

    cJSON* root = cJSON_CreateObject();
    cJSON* list = cJSON_AddArrayToObject(root, "networks");
    for (int i = 0; i < count; i++) {
        const char* ssid = (const char*)recs[i].ssid;
        if (ssid[0] == 0 || strcmp(ssid, AP_SSID) == 0)
            continue;
        // Gleiche SSID von mehreren Routern nur einmal zeigen (staerkstes Signal zuerst).
        bool dup = false;
        for (int j = 0; j < i; j++)
            if (strcmp(ssid, (const char*)recs[j].ssid) == 0)
                dup = true;
        if (dup)
            continue;
        cJSON* n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "ssid", ssid);
        cJSON_AddNumberToObject(n, "rssi", recs[i].rssi);
        cJSON_AddBoolToObject(n, "secured", recs[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(list, n);
    }
    free(recs);
    cJSON_AddStringToObject(root, "current", wifi_cfg.sta_ssid);
    return send_json(req, root);
}

// "192.168.1.10" reicht: Schema und Port ergaenzen
static void set_mqtt_uri(pageflippy_mqtt_config_t* cfg, const char* u) {
    if (u[0] && strstr(u, "://") == NULL)
        snprintf(cfg->uri, sizeof(cfg->uri), "mqtt://%s%s", u, strchr(u, ':') ? "" : ":1883");
    else
        snprintf(cfg->uri, sizeof(cfg->uri), "%s", u);
}

static esp_err_t mqtt_post(httpd_req_t* req) {
    cJSON* root = read_json(req);
    if (root == NULL)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, T("Ungueltige Daten", "Invalid data"));
    pageflippy_mqtt_config_t cfg;
    pageflippy_mqtt_config_load(&cfg);
    const cJSON* item;
    if ((item = cJSON_GetObjectItem(root, "enabled")) && cJSON_IsBool(item))
        cfg.enabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "uri")) && cJSON_IsString(item))
        set_mqtt_uri(&cfg, item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "user")) && cJSON_IsString(item))
        snprintf(cfg.user, sizeof(cfg.user), "%s", item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "pass")) && cJSON_IsString(item) && item->valuestring[0])
        snprintf(cfg.pass, sizeof(cfg.pass), "%s", item->valuestring);
    cJSON_Delete(root);

    pageflippy_mqtt_config_save(&cfg);
    logi(T("pageflippy: MQTT-Einstellungen gespeichert, Neustart\n", "pageflippy: MQTT settings saved, restarting\n"));
    send_ok(req);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

//
// Sicherung: Belegung, WLAN und MQTT als JSON-Datei exportieren und wieder einspielen.
// Bluetooth-Kopplungen gehoeren nicht dazu (die Schluessel gelten nur fuer diesen ESP32).
//
#define BACKUP_FORMAT 1

static esp_err_t backup_get(httpd_req_t* req) {
    char query[16] = {0};
    char pw[4] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "pw", pw, sizeof(pw));
    bool with_pw = strcmp(pw, "1") == 0;

    const pageflippy_config_t* cfg = &pageflippy_config;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "pageflippy_backup", BACKUP_FORMAT);
    cJSON_AddStringToObject(root, "lang", T("de", "en"));

    cJSON* map = cJSON_AddObjectToObject(root, "mapping");
    cJSON_AddItemToObject(map, "next", mask_to_json(cfg->next_mask));
    cJSON_AddItemToObject(map, "prev", mask_to_json(cfg->prev_mask));
    cJSON_AddItemToObject(map, "next_keys", keys_to_json(cfg->next_keys));
    cJSON_AddItemToObject(map, "prev_keys", keys_to_json(cfg->prev_keys));
    cJSON_AddNumberToObject(map, "out_next", cfg->out_next);
    cJSON_AddNumberToObject(map, "out_prev", cfg->out_prev);
    cJSON_AddBoolToObject(map, "rumble", cfg->rumble);
    cJSON_AddNumberToObject(map, "idle_off_min", cfg->idle_off_s / 60.0);
    cJSON_AddBoolToObject(map, "touch", cfg->touch_enabled);

    cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "sta_ssid", wifi_cfg.sta_ssid);
    if (with_pw) {
        cJSON_AddStringToObject(wifi, "sta_pass", wifi_cfg.sta_pass);
        cJSON_AddStringToObject(wifi, "ap_pass", wifi_cfg.ap_pass);
    }

    pageflippy_mqtt_config_t mq;
    pageflippy_mqtt_config_load(&mq);
    cJSON* mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddBoolToObject(mqtt, "enabled", mq.enabled);
    cJSON_AddStringToObject(mqtt, "uri", mq.uri);
    cJSON_AddStringToObject(mqtt, "user", mq.user);
    if (with_pw)
        cJSON_AddStringToObject(mqtt, "pass", mq.pass);

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == NULL)
        return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"pageflippy-backup.json\"");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}

static esp_err_t backup_post(httpd_req_t* req) {
    cJSON* root = read_json(req);
    const cJSON* fmt = root ? cJSON_GetObjectItem(root, "pageflippy_backup") : NULL;
    if (root == NULL || !cJSON_IsNumber(fmt) || fmt->valueint < 1 || fmt->valueint > BACKUP_FORMAT) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   T("Keine gueltige Pageflippy-Sicherung", "Not a valid Pageflippy backup"));
    }
    const cJSON* sec;
    const cJSON* item;

    if ((sec = cJSON_GetObjectItem(root, "mapping")) && cJSON_IsObject(sec)) {
        pageflippy_config_t cfg = pageflippy_config;
        apply_mapping(sec, &cfg);
        pageflippy_config = cfg;
        pageflippy_config_save();
    }

    if ((sec = cJSON_GetObjectItem(root, "wifi")) && cJSON_IsObject(sec)) {
        pageflippy_wifi_config_t cfg = wifi_cfg;
        if ((item = cJSON_GetObjectItem(sec, "sta_ssid")) && cJSON_IsString(item)) {
            bool new_net = strcmp(item->valuestring, cfg.sta_ssid) != 0;
            snprintf(cfg.sta_ssid, sizeof(cfg.sta_ssid), "%s", item->valuestring);
            // Ohne Passwort in der Datei bleibt das gespeicherte, solange es dasselbe Netz ist.
            if (new_net)
                cfg.sta_pass[0] = 0;
        }
        if ((item = cJSON_GetObjectItem(sec, "sta_pass")) && cJSON_IsString(item))
            snprintf(cfg.sta_pass, sizeof(cfg.sta_pass), "%s", item->valuestring);
        if (cfg.sta_ssid[0] == 0)
            cfg.sta_pass[0] = 0;
        if ((item = cJSON_GetObjectItem(sec, "ap_pass")) && cJSON_IsString(item) && strlen(item->valuestring) >= 8)
            snprintf(cfg.ap_pass, sizeof(cfg.ap_pass), "%s", item->valuestring);
        pageflippy_wifi_config_save(&cfg);
    }

    if ((sec = cJSON_GetObjectItem(root, "mqtt")) && cJSON_IsObject(sec)) {
        pageflippy_mqtt_config_t cfg;
        pageflippy_mqtt_config_load(&cfg);
        if ((item = cJSON_GetObjectItem(sec, "enabled")) && cJSON_IsBool(item))
            cfg.enabled = cJSON_IsTrue(item);
        if ((item = cJSON_GetObjectItem(sec, "uri")) && cJSON_IsString(item))
            set_mqtt_uri(&cfg, item->valuestring);
        if ((item = cJSON_GetObjectItem(sec, "user")) && cJSON_IsString(item))
            snprintf(cfg.user, sizeof(cfg.user), "%s", item->valuestring);
        if ((item = cJSON_GetObjectItem(sec, "pass")) && cJSON_IsString(item))
            snprintf(cfg.pass, sizeof(cfg.pass), "%s", item->valuestring);
        pageflippy_mqtt_config_save(&cfg);
    }
    cJSON_Delete(root);

    logi(T("pageflippy: Sicherung eingespielt, Neustart\n", "pageflippy: backup restored, restarting\n"));
    send_ok(req);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

//
// Firmware-Update: Die Update-Datei kommt als reiner Dateiinhalt im POST.
//
#define UPDATE_CHUNK 4096

static esp_err_t update_error(httpd_req_t* req) {
    pageflippy_update_status_t up;
    pageflippy_update_get_status(&up);
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, up.message[0] ? up.message : "Update");
}

static esp_err_t update_post(httpd_req_t* req) {
    char* buf = malloc(UPDATE_CHUNK);
    if (buf == NULL)
        return httpd_resp_send_500(req);
    esp_err_t err = pageflippy_update_begin(req->content_len);
    if (err == ESP_ERR_INVALID_STATE) {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, T("Update laeuft schon", "Update already running"));
    }
    if (err != ESP_OK) {
        free(buf);
        return update_error(req);
    }

    size_t left = req->content_len;
    bool ok = true;
    while (left > 0 && ok) {
        // Block ganz fuellen: der erste muss fuer die Pruefung der Datei gross genug sein.
        size_t want = left < UPDATE_CHUNK ? left : UPDATE_CHUNK;
        size_t got = 0;
        while (got < want) {
            int r = httpd_req_recv(req, buf + got, want - got);
            if (r == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            if (r <= 0) {
                ok = false;
                break;
            }
            got += r;
        }
        if (!ok)
            break;
        if (pageflippy_update_write(buf, got) != ESP_OK) {
            free(buf);
            return update_error(req);
        }
        left -= got;
    }
    free(buf);
    if (pageflippy_update_finish(ok) != ESP_OK)
        return ok ? update_error(req) : ESP_FAIL;

    send_ok(req);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t action_post(httpd_req_t* req) {
    char query[32] = {0};
    char what[16] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "do", what, sizeof(what));

    if (strcmp(what, "next") == 0) {
        pageflippy_request_flip(true);
    } else if (strcmp(what, "prev") == 0) {
        pageflippy_request_flip(false);
    } else if (strcmp(what, "btoff") == 0) {
        pageflippy_request_bt_off();
    } else if (strcmp(what, "pair") == 0) {
        pageflippy_request_pairing();
    } else if (strcmp(what, "defaults") == 0) {
        pageflippy_config_reset();
        pageflippy_config_save();
    } else if (strcmp(what, "ghupdate") == 0) {
        if (sta_ip[0] == 0)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       T("Dafuer muss das Heim-WLAN verbunden sein", "Home Wi-Fi must be connected for this"));
        esp_err_t err = pageflippy_update_start_github();
        if (err == ESP_ERR_INVALID_STATE)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, T("Update laeuft schon", "Update already running"));
        if (err != ESP_OK)
            return update_error(req);
    } else if (strcmp(what, "forget") == 0) {
        // Loescht alle Bluetooth-Kopplungen: Controller und Tolino muessen neu gekoppelt werden.
        uni_bt_del_keys_safe();
    } else if (strcmp(what, "reboot") == 0) {
        send_ok(req);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, T("Unbekannte Aktion", "Unknown action"));
    }
    return send_ok(req);
}

static void http_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;
    // Bluetooth belegt viel Funkzeit: Senden darf laenger dauern als die Standard-5-Sekunden.
    config.send_wait_timeout = 20;
    config.recv_wait_timeout = 20;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        loge(T("pageflippy: Webserver konnte nicht starten\n", "pageflippy: web server could not start\n"));
        return;
    }

    static const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get},
        {.uri = "/api/config", .method = HTTP_GET, .handler = config_get},
        {.uri = "/api/config", .method = HTTP_POST, .handler = config_post},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = scan_get},
        {.uri = "/api/mqtt", .method = HTTP_POST, .handler = mqtt_post},
        {.uri = "/api/action", .method = HTTP_POST, .handler = action_post},
        {.uri = "/api/backup", .method = HTTP_GET, .handler = backup_get},
        {.uri = "/api/backup", .method = HTTP_POST, .handler = backup_post},
        {.uri = "/api/update", .method = HTTP_POST, .handler = update_post},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
        httpd_register_uri_handler(server, &uris[i]);
}

//
// Konsole: WLAN ohne Weboberflaeche einrichten
//
static struct {
    struct arg_str* ssid;
    struct arg_str* psk;
    struct arg_end* end;
} wifi_set_args;

static void save_and_restart(const pageflippy_wifi_config_t* cfg) {
    pageflippy_wifi_config_save(cfg);
    printf(T("Gespeichert, Neustart ...\n", "Saved, restarting ...\n"));
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static int cmd_wifi_set(int argc, char** argv) {
    if (arg_parse(argc, argv, (void**)&wifi_set_args) != 0) {
        arg_print_errors(stderr, wifi_set_args.end, argv[0]);
        return 1;
    }
    const char* ssid = wifi_set_args.ssid->sval[0];
    const char* psk = wifi_set_args.psk->count ? wifi_set_args.psk->sval[0] : "";
    if (strlen(ssid) == 0 || strlen(ssid) > 32) {
        printf(T("SSID muss 1 bis 32 Zeichen lang sein\n", "SSID must be 1 to 32 characters\n"));
        return 1;
    }
    if (psk[0] && (strlen(psk) < 8 || strlen(psk) > 64)) {
        printf(T("Passwort muss 8 bis 64 Zeichen lang sein (oder leer fuer offenes WLAN)\n",
                 "Password must be 8 to 64 characters (or empty for an open network)\n"));
        return 1;
    }
    pageflippy_wifi_config_t cfg = wifi_cfg;
    snprintf(cfg.sta_ssid, sizeof(cfg.sta_ssid), "%s", ssid);
    snprintf(cfg.sta_pass, sizeof(cfg.sta_pass), "%s", psk);
    printf(T("Heim-WLAN: \"%s\"\n", "Home Wi-Fi: \"%s\"\n"), ssid);
    save_and_restart(&cfg);
    return 0;
}

static int cmd_wifi_clear(int argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    pageflippy_wifi_config_t cfg = wifi_cfg;
    cfg.sta_ssid[0] = 0;
    cfg.sta_pass[0] = 0;
    printf(T("Heim-WLAN entfernt, nur Hotspot\n", "Home Wi-Fi removed, hotspot only\n"));
    save_and_restart(&cfg);
    return 0;
}

static int cmd_wifi_status(int argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (wifi_off)
        printf(T("WLAN:      aus bis zum Neustart (BOOT 3 s = Hotspot an)\n", "Wi-Fi:     off until restart (BOOT 3 s = hotspot on)\n"));
    else if (ap_on)
        printf(T("Hotspot:   \"%s\" an, http://192.168.4.1\n", "Hotspot:   \"%s\" on, http://192.168.4.1\n"), AP_SSID);
    else
        printf(T("Hotspot:   aus\n", "Hotspot:   off\n"));
    if (wifi_cfg.sta_ssid[0] == 0)
        printf(T("Heim-WLAN: keins\n", "Home Wi-Fi: none\n"));
    else if (sta_ip[0])
        printf(T("Heim-WLAN: \"%s\", verbunden, http://%s\n", "Home Wi-Fi: \"%s\", connected, http://%s\n"),
               wifi_cfg.sta_ssid, sta_ip);
    else
        printf(T("Heim-WLAN: \"%s\", nicht verbunden\n", "Home Wi-Fi: \"%s\", not connected\n"), wifi_cfg.sta_ssid);
    return 0;
}

void pageflippy_web_register_console_cmds(void) {
    wifi_set_args.ssid = arg_str1(NULL, NULL, "<ssid>", T("Name des Heim-WLANs (mit Leerzeichen in \"...\")",
                                                             "home Wi-Fi name (use \"...\" for spaces)"));
    wifi_set_args.psk = arg_str0(NULL, NULL, "<psk>", T("Passwort (leer = offenes WLAN)", "password (empty = open network)"));
    wifi_set_args.end = arg_end(2);
    static const esp_console_cmd_t cmds[] = {
        {.command = "wifi_set",
         .help = T("Heim-WLAN eintragen und neu starten", "Set home Wi-Fi and restart"),
         .func = &cmd_wifi_set,
         .argtable = &wifi_set_args},
        {.command = "wifi_clear",
         .help = T("Heim-WLAN entfernen (nur Hotspot) und neu starten", "Remove home Wi-Fi (hotspot only) and restart"),
         .func = &cmd_wifi_clear},
        {.command = "wifi_status",
         .help = T("WLAN-Status anzeigen", "Show Wi-Fi status"),
         .func = &cmd_wifi_status},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
}

// BOOT-Taste am Board: startet die Controller-Suche.
static void boot_button_task(void* arg) {
    ARG_UNUSED(arg);
    const gpio_num_t pin = GPIO_NUM_0;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    // Kurz druecken: Controller-Suche. 3 Sekunden halten: Hotspot bis zum Neustart an.
    int held = 0;
    while (true) {
        bool down = gpio_get_level(pin) == 0;
        if (down) {
            held++;
            if (held == 3000 / 50) {
                logi(T("pageflippy: BOOT-Taste lang: Hotspot an bis Neustart\n",
                       "pageflippy: BOOT button long: hotspot on until restart\n"));
                force_ap();
            }
        } else {
            if (held > 0 && held < 3000 / 50) {
                logi(T("pageflippy: BOOT-Taste gedrueckt\n", "pageflippy: BOOT button pressed\n"));
                pageflippy_request_pairing();
            }
            held = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void pageflippy_web_start(void) {
    wifi_start();
    http_start();
    pageflippy_update_init();
    pageflippy_touch_start();
    xTaskCreate(boot_button_task, "boot_button", 3072, NULL, 2, NULL);
}
