// ESP32Pageflippy: Firmware-Update ohne Datenverlust (Datei-Upload oder direkt von GitHub).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <esp_err.h>

typedef enum {
    UPDATE_IDLE = 0,
    UPDATE_RUNNING,
    UPDATE_DONE,    // geschrieben, Neustart folgt
    UPDATE_FAILED,
} pageflippy_update_state_t;

typedef struct {
    pageflippy_update_state_t state;
    int percent;      // 0..100, -1 = unbekannt
    char message[96]; // Fehlertext bei UPDATE_FAILED
} pageflippy_update_status_t;

// Installierte Version, z. B. "1.0.0"
const char* pageflippy_update_version(void);
// Repository fuer Updates, z. B. "Tschitty/ESP32Pageflippy"
const char* pageflippy_update_repo(void);
void pageflippy_update_get_status(pageflippy_update_status_t* out);

// Datei-Upload: begin, beliebig viele write, dann finish (ok = alles empfangen). Bei Fehler steht
// der Grund im Status. Nach erfolgreichem finish muss der Aufrufer neu starten.
esp_err_t pageflippy_update_begin(size_t total_len);
esp_err_t pageflippy_update_write(const void* data, size_t len);
esp_err_t pageflippy_update_finish(bool ok);

// Laedt die neueste Version aus den GitHub-Releases und startet danach neu (eigener Task).
// Braucht eine Internetverbindung ueber das Heim-WLAN.
esp_err_t pageflippy_update_start_github(void);

// Nach dem Start aufrufen: bestaetigt die laufende Version nach kurzer Zeit als "gut".
void pageflippy_update_init(void);
