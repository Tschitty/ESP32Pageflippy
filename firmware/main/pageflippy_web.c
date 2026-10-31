// ESP32Pageflippy: WLAN (Hotspot und/oder Heimnetz) und Weboberflaeche.
//
// - Hotspot "ESP32Pageflippy", Passwort "pageflippy", Adresse http://192.168.4.1
// - Ist ein Heim-WLAN eingetragen, verbindet sich der ESP32 zusaetzlich damit.
//   Klappt das nicht innerhalb von 20 Sekunden, bleibt nur der Hotspot an.

#include "pageflippy_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
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

#define AP_SSID "ESP32Pageflippy"
#define STA_TIMEOUT_US (20 * 1000 * 1000)

#ifdef PAGEFLIPPY_LANG_EN
extern const char index_html_start[] asm("_binary_index_en_html_gz_start");
extern const char index_html_end[] asm("_binary_index_en_html_gz_end");
#else
extern const char index_html_start[] asm("_binary_index_de_html_gz_start");
extern const char index_html_end[] asm("_binary_index_de_html_gz_end");
#endif

static pageflippy_wifi_config_t wifi_cfg;
static char sta_ip[16];
static bool sta_gave_up;
static esp_timer_handle_t sta_timer;

//
// WLAN
//
static void sta_timeout(void* arg) {
    ARG_UNUSED(arg);
    if (sta_ip[0] == 0) {
        logi(T("pageflippy: Heim-WLAN \"%s\" nicht erreichbar, nur Hotspot aktiv\n", "pageflippy: home Wi-Fi \"%s\" not reachable, hotspot only\n"), wifi_cfg.sta_ssid);
        sta_gave_up = true;
        esp_wifi_disconnect();
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    ARG_UNUSED(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (wifi_cfg.sta_ssid[0])
            esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        sta_ip[0] = 0;
        if (!sta_gave_up && wifi_cfg.sta_ssid[0])
            esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* ev = (ip_event_got_ip_t*)data;
        snprintf(sta_ip, sizeof(sta_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        logi(T("pageflippy: Heim-WLAN verbunden, Weboberflaeche unter http://%s\n", "pageflippy: home Wi-Fi connected, web interface at http://%s\n"), sta_ip);
    }
}

static void wifi_start(void) {
    pageflippy_wifi_config_load(&wifi_cfg);
    bool use_sta = wifi_cfg.sta_ssid[0] != 0;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    // STA immer anlegen: wird auch fuer die WLAN-Suche gebraucht.
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t ap = {0};
    strcpy((char*)ap.ap.ssid, AP_SSID);
    ap.ap.ssid_len = strlen(AP_SSID);
    strncpy((char*)ap.ap.password, wifi_cfg.ap_pass, sizeof(ap.ap.password) - 1);
    ap.ap.channel = 6;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (use_sta) {
        wifi_config_t sta = {0};
        strncpy((char*)sta.sta.ssid, wifi_cfg.sta_ssid, sizeof(sta.sta.ssid) - 1);
        strncpy((char*)sta.sta.password, wifi_cfg.sta_pass, sizeof(sta.sta.password) - 1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));

        const esp_timer_create_args_t args = {.callback = sta_timeout, .name = "sta_timeout"};
        esp_timer_create(&args, &sta_timer);
        esp_timer_start_once(sta_timer, STA_TIMEOUT_US);
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    // Funk-Stromsparen aus: sonst reagiert die Seite traege. Bluetooth teilt sich die Antenne.
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    logi(T("pageflippy: Hotspot \"%s\" aktiv, Weboberflaeche unter http://192.168.4.1\n", "pageflippy: hotspot \"%s\" active, web interface at http://192.168.4.1\n"), AP_SSID);
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
    if (req->content_len == 0 || req->content_len > 2048)
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
    return send_json(req, root);
}

static esp_err_t config_post(httpd_req_t* req) {
    cJSON* root = read_json(req);
    if (root == NULL)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, T("Ungueltige Daten", "Invalid data"));

    pageflippy_config_t cfg = pageflippy_config;
    const cJSON* item;
    if ((item = cJSON_GetObjectItem(root, "next")) && cJSON_IsArray(item))
        cfg.next_mask = json_to_mask(item);
    if ((item = cJSON_GetObjectItem(root, "prev")) && cJSON_IsArray(item))
        cfg.prev_mask = json_to_mask(item);
    if ((item = cJSON_GetObjectItem(root, "next_keys")) && cJSON_IsArray(item))
        json_to_keys(item, cfg.next_keys);
    if ((item = cJSON_GetObjectItem(root, "prev_keys")) && cJSON_IsArray(item))
        json_to_keys(item, cfg.prev_keys);
    if ((item = cJSON_GetObjectItem(root, "out_next")) && cJSON_IsNumber(item) && item->valueint > 0 &&
        item->valueint < 256)
        cfg.out_next = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "out_prev")) && cJSON_IsNumber(item) && item->valueint > 0 &&
        item->valueint < 256)
        cfg.out_prev = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "rumble")) && cJSON_IsBool(item))
        cfg.rumble = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "touch")) && cJSON_IsBool(item))
        cfg.touch_enabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "idle_off_min")) && cJSON_IsNumber(item) && item->valuedouble >= 0 &&
        item->valuedouble <= 600)
        cfg.idle_off_s = (uint16_t)(item->valuedouble * 60);
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
    // Laeuft gerade noch ein Verbindungsversuch, stoert der die Suche: abbrechen.
    if (sta_ip[0] == 0 && !sta_gave_up) {
        sta_gave_up = true;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    wifi_scan_config_t scan = {.show_hidden = false};
    esp_err_t err = esp_wifi_scan_start(&scan, true);
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
    config.stack_size = 6144;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
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
        {.uri = "/api/action", .method = HTTP_POST, .handler = action_post},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
        httpd_register_uri_handler(server, &uris[i]);
}

// BOOT-Taste am Board: startet die Controller-Suche.
static void boot_button_task(void* arg) {
    ARG_UNUSED(arg);
    const gpio_num_t pin = GPIO_NUM_0;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    bool was_down = false;
    while (true) {
        bool down = gpio_get_level(pin) == 0;
        if (down && !was_down) {
            logi(T("pageflippy: BOOT-Taste gedrueckt\n", "pageflippy: BOOT button pressed\n"));
            pageflippy_request_pairing();
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void pageflippy_web_start(void) {
    wifi_start();
    http_start();
    pageflippy_touch_start();
    xTaskCreate(boot_button_task, "boot_button", 2048, NULL, 2, NULL);
}
