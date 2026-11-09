// ESP32Pageflippy: Firmware-Update ohne Datenverlust.
//
// Der Flash hat zwei App-Bereiche (ota_0 / ota_1). Ein Update schreibt die neue Version in den
// gerade unbenutzten Bereich und schaltet erst nach erfolgreicher Pruefung um. Belegung, WLAN,
// MQTT und Bluetooth-Kopplungen liegen in der NVS-Partition und bleiben dabei erhalten.
// Startet die neue Version nicht sauber, kehrt der Bootloader zur alten zurueck.

#include "pageflippy_update.h"

#include <stdio.h>
#include <string.h>

#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <uni.h>

#include "pageflippy_lang.h"
#include "pageflippy_status.h"

#ifndef PAGEFLIPPY_UPDATE_REPO
#define PAGEFLIPPY_UPDATE_REPO ""
#endif

#define UPDATE_URL \
    "https://github.com/" PAGEFLIPPY_UPDATE_REPO "/releases/latest/download/ESP32Pageflippy_" T("DE", "EN") "_update.bin"
// So lange muss eine neue Version laufen, bevor sie als "gut" gilt.
#define CONFIRM_AFTER_US (30 * 1000 * 1000)
// Der App-Beschreibungsblock steht direkt hinter dem Image- und dem ersten Segment-Header.
#define APP_DESC_OFFSET (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static pageflippy_update_status_t status = {.percent = -1};

static esp_ota_handle_t ota_handle;
static const esp_partition_t* ota_part;
static size_t ota_total, ota_done;
static bool ota_checked;

static void set_status(pageflippy_update_state_t state, int percent, const char* message) {
    taskENTER_CRITICAL(&lock);
    status.state = state;
    status.percent = percent;
    if (message != NULL)
        snprintf(status.message, sizeof(status.message), "%s", message);
    taskEXIT_CRITICAL(&lock);
}

static esp_err_t fail(const char* message) {
    loge(T("pageflippy: Update fehlgeschlagen: %s\n", "pageflippy: update failed: %s\n"), message);
    set_status(UPDATE_FAILED, -1, message);
    return ESP_FAIL;
}

static bool busy(void) {
    taskENTER_CRITICAL(&lock);
    bool b = status.state == UPDATE_RUNNING || status.state == UPDATE_DONE;
    taskEXIT_CRITICAL(&lock);
    return b;
}

const char* pageflippy_update_version(void) {
    return esp_app_get_description()->version;
}

const char* pageflippy_update_repo(void) {
    return PAGEFLIPPY_UPDATE_REPO;
}

void pageflippy_update_get_status(pageflippy_update_status_t* out) {
    taskENTER_CRITICAL(&lock);
    *out = status;
    taskEXIT_CRITICAL(&lock);
}

// Passt die Datei zu diesem Geraet? Verhindert, dass ein fremdes oder das Komplett-Image eingespielt wird.
static const char* check_image(const uint8_t* data, size_t len) {
    if (len < APP_DESC_OFFSET + sizeof(esp_app_desc_t))
        return T("Datei ist zu klein", "File is too small");
    if (data[0] != ESP_IMAGE_HEADER_MAGIC)
        return T("Keine Update-Datei. Bitte die Datei mit _update im Namen nehmen, nicht das Komplett-Image",
                 "Not an update file. Use the file with _update in its name, not the full image");
    const esp_app_desc_t* desc = (const esp_app_desc_t*)(data + APP_DESC_OFFSET);
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
        strncmp(desc->project_name, esp_app_get_description()->project_name, sizeof(desc->project_name)) != 0)
        return T("Das ist keine Pageflippy-Firmware", "This is not a Pageflippy firmware");
    return NULL;
}

//
// Datei-Upload
//
esp_err_t pageflippy_update_begin(size_t total_len) {
    if (busy())
        return ESP_ERR_INVALID_STATE;
    ota_part = esp_ota_get_next_update_partition(NULL);
    if (ota_part == NULL)
        return fail(T("Kein Update-Bereich im Flash. Einmalig das Komplett-Image flashen",
                      "No update area in flash. Flash the full image once"));
    if (total_len == 0 || total_len > ota_part->size)
        return fail(T("Datei passt nicht in den Update-Bereich", "File does not fit into the update area"));
    set_status(UPDATE_RUNNING, 0, "");
    // Bluetooth aus: mehr Funkzeit fuer den Upload. Nach dem Update startet der ESP32 ohnehin neu.
    pageflippy_request_bt_off();
    // Loescht den Flash stueckweise beim Schreiben statt alles vorab (das wuerde Sekunden blockieren).
    if (esp_ota_begin(ota_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle) != ESP_OK)
        return fail(T("Update konnte nicht gestartet werden", "Update could not be started"));
    ota_total = total_len;
    ota_done = 0;
    ota_checked = false;
    return ESP_OK;
}

esp_err_t pageflippy_update_write(const void* data, size_t len) {
    if (!ota_checked) {
        // Der erste Block muss gross genug fuer die Pruefung sein (der Aufrufer liefert volle Bloecke).
        const char* problem = check_image(data, len);
        if (problem != NULL) {
            esp_ota_abort(ota_handle);
            return fail(problem);
        }
        ota_checked = true;
    }
    if (esp_ota_write(ota_handle, data, len) != ESP_OK) {
        esp_ota_abort(ota_handle);
        return fail(T("Schreiben in den Flash fehlgeschlagen", "Writing to flash failed"));
    }
    ota_done += len;
    set_status(UPDATE_RUNNING, (int)(ota_done * 100 / ota_total), NULL);
    return ESP_OK;
}

esp_err_t pageflippy_update_finish(bool ok) {
    if (!ok || ota_done != ota_total) {
        esp_ota_abort(ota_handle);
        return fail(T("Upload abgebrochen", "Upload interrupted"));
    }
    // Prueft die Pruefsumme des ganzen Images.
    if (esp_ota_end(ota_handle) != ESP_OK)
        return fail(T("Datei ist beschaedigt (Pruefsumme falsch)", "File is damaged (checksum mismatch)"));
    if (esp_ota_set_boot_partition(ota_part) != ESP_OK)
        return fail(T("Umschalten auf die neue Version fehlgeschlagen", "Switching to the new version failed"));
    logi(T("pageflippy: Update geschrieben, Neustart\n", "pageflippy: update written, restarting\n"));
    set_status(UPDATE_DONE, 100, "");
    return ESP_OK;
}

//
// Update von GitHub
//
static void github_task(void* arg) {
    ARG_UNUSED(arg);
    logi("pageflippy: Update: %s\n", UPDATE_URL);

    esp_http_client_config_t http = {
        .url = UPDATE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .keep_alive_enable = true,
        // GitHub leitet auf eine sehr lange, signierte Adresse um: die muss in beide Puffer passen.
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
    };
    esp_https_ota_config_t cfg = {.http_config = &http};
    esp_https_ota_handle_t handle = NULL;
    const char* problem = NULL;

    if (esp_https_ota_begin(&cfg, &handle) != ESP_OK) {
        problem = T("Download von GitHub nicht moeglich (Internet? Release vorhanden?)",
                    "Download from GitHub not possible (internet? release published?)");
        handle = NULL;
    } else {
        esp_app_desc_t desc;
        if (esp_https_ota_get_img_desc(handle, &desc) != ESP_OK ||
            strncmp(desc.project_name, esp_app_get_description()->project_name, sizeof(desc.project_name)) != 0) {
            problem = T("Das ist keine Pageflippy-Firmware", "This is not a Pageflippy firmware");
        } else {
            esp_err_t err;
            while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                int total = esp_https_ota_get_image_size(handle);
                int done = esp_https_ota_get_image_len_read(handle);
                set_status(UPDATE_RUNNING, total > 0 ? (int)((int64_t)done * 100 / total) : -1, NULL);
            }
            if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle))
                problem = T("Download abgebrochen", "Download interrupted");
        }
    }

    if (problem == NULL) {
        if (esp_https_ota_finish(handle) != ESP_OK)
            problem = T("Datei ist beschaedigt (Pruefsumme falsch)", "File is damaged (checksum mismatch)");
    } else if (handle != NULL) {
        esp_https_ota_abort(handle);
    }

    if (problem != NULL) {
        fail(problem);
        vTaskDelete(NULL);
        return;
    }
    logi(T("pageflippy: Update geschrieben, Neustart\n", "pageflippy: update written, restarting\n"));
    set_status(UPDATE_DONE, 100, "");
    vTaskDelay(pdMS_TO_TICKS(3000));  // die Weboberflaeche soll "fertig" noch abholen koennen
    esp_restart();
}

esp_err_t pageflippy_update_start_github(void) {
    if (busy())
        return ESP_ERR_INVALID_STATE;
    if (PAGEFLIPPY_UPDATE_REPO[0] == 0)
        return fail(T("Kein GitHub-Repository eingestellt", "No GitHub repository configured"));
    if (esp_ota_get_next_update_partition(NULL) == NULL)
        return fail(T("Kein Update-Bereich im Flash. Einmalig das Komplett-Image flashen",
                      "No update area in flash. Flash the full image once"));
    set_status(UPDATE_RUNNING, 0, "");
    // Bluetooth aus: mehr Funkzeit und Rechenzeit fuer den verschluesselten Download.
    pageflippy_request_bt_off();
    if (xTaskCreate(github_task, "update", 8192, NULL, 5, NULL) != pdPASS)
        return fail(T("Zu wenig Speicher fuer das Update", "Not enough memory for the update"));
    return ESP_OK;
}

//
// Neue Version bestaetigen
//
static void confirm_cb(void* arg) {
    ARG_UNUSED(arg);
    esp_ota_img_states_t state;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        logi(T("pageflippy: neue Version %s bestaetigt\n", "pageflippy: new version %s confirmed\n"),
             pageflippy_update_version());
    }
}

void pageflippy_update_init(void) {
    logi("pageflippy: Version %s (%s)\n", pageflippy_update_version(), esp_ota_get_running_partition()->label);
    static esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {.callback = confirm_cb, .name = "update_ok"};
    if (esp_timer_create(&args, &timer) == ESP_OK)
        esp_timer_start_once(timer, CONFIRM_AFTER_US);
}
