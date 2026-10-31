// ESP32Pageflippy: Zustand fuer die Weboberflaeche und Aktionen aus dem Webserver.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PAGEFLIPPY_STATUS_MAX_CONTROLLERS 4

typedef struct {
    bool tolino_connected;
    int controller_count;
    char controllers[PAGEFLIPPY_STATUS_MAX_CONTROLLERS][32];
    // Zuletzt gedrueckte Eingabe, zum Herausfinden der Belegung
    char last_input[48];
    uint32_t flips;
    // Restzeit der Controller-Suche in Sekunden, 0 = Suche aus
    uint32_t pairing_left_s;
    bool bt_off;
} pageflippy_status_t;

// Thread-sicher, darf aus dem Webserver aufgerufen werden.
void pageflippy_status_get(pageflippy_status_t* out);

// Blaettert testweise vor oder zurueck. Thread-sicher.
void pageflippy_request_flip(bool next);

// Sucht fuer eine Weile nach neuen Controllern. Thread-sicher.
void pageflippy_request_pairing(void);
// Schaltet Bluetooth bis zum naechsten Neustart aus (WLAN bekommt den Funk allein).
void pageflippy_request_bt_off(void);
