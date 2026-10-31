// ESP32Pageflippy: Tastenbelegung und WLAN-Einstellungen (in NVS gespeichert).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <controller/uni_gamepad.h>

#define PAGEFLIPPY_MAX_KEYS 4

// Controller-Eingaben, die belegt werden koennen. Index = Bit in next_mask / prev_mask.
typedef enum {
    INPUT_DPAD_UP,
    INPUT_DPAD_DOWN,
    INPUT_DPAD_LEFT,
    INPUT_DPAD_RIGHT,
    INPUT_A,
    INPUT_B,
    INPUT_X,
    INPUT_Y,
    INPUT_L1,
    INPUT_R1,
    INPUT_L2,
    INPUT_R2,
    INPUT_L3,
    INPUT_R3,
    INPUT_SYSTEM,
    INPUT_SELECT,
    INPUT_START,
    INPUT_CAPTURE,
    INPUT_STICK_L_LEFT,
    INPUT_STICK_L_RIGHT,
    INPUT_STICK_R_LEFT,
    INPUT_STICK_R_RIGHT,
    INPUT_TOUCH_SWIPE_LEFT,
    INPUT_TOUCH_SWIPE_RIGHT,
    INPUT_TOUCH_CLICK,
    INPUT_TOUCH_CLICK_RIGHT,
    INPUT_COUNT,
} pageflippy_input_t;

typedef struct {
    // Controller: welche Eingaben blaettern vor / zurueck
    uint32_t next_mask;
    uint32_t prev_mask;
    // Tastaturen und Fernbedienungen: HID-Codes, die vor / zurueck ausloesen (0 = leer)
    uint8_t next_keys[PAGEFLIPPY_MAX_KEYS];
    uint8_t prev_keys[PAGEFLIPPY_MAX_KEYS];
    // HID-Codes, die an den Tolino gesendet werden
    uint8_t out_next;
    uint8_t out_prev;
    bool rumble;
    // Controller ausschalten, wenn der Tolino so lange nicht verbunden ist (Sekunden, 0 = nie).
    // Neue Felder immer hinten anfuegen: aeltere gespeicherte Belegungen werden dann weiter geladen.
    uint16_t idle_off_s;
    // Kapazitive Touch-Tasten am ESP32 (GPIO32 = vor, GPIO33 = zurueck)
    bool touch_enabled;
} pageflippy_config_t;

typedef struct {
    char sta_ssid[33];
    char sta_pass[65];
    char ap_pass[65];
} pageflippy_wifi_config_t;

// Aktuelle Belegung. Wird vom Webserver geschrieben und vom Bluetooth-Thread gelesen.
extern pageflippy_config_t pageflippy_config;

void pageflippy_config_load(void);
void pageflippy_config_save(void);
// Zaehlt jedes Speichern mit; die Weboberflaeche laedt die Belegung neu, wenn er sich aendert.
uint32_t pageflippy_config_revision(void);
void pageflippy_config_reset(void);
// Nur die Tastenbelegung auf Standard setzen, sonstige Einstellungen behalten.
void pageflippy_config_reset_mapping(void);
// Eingabe dauerhaft "vor" (next = true) oder "zurueck" zuordnen, aus der anderen Richtung entfernen.
void pageflippy_config_assign(pageflippy_input_t input, bool next);

void pageflippy_wifi_config_load(pageflippy_wifi_config_t* out);
void pageflippy_wifi_config_save(const pageflippy_wifi_config_t* cfg);

const char* pageflippy_input_name(pageflippy_input_t input);

// Bitmaske aller gerade gedrueckten Eingaben eines Gamepads.
uint32_t pageflippy_gamepad_mask(const uni_gamepad_t* gp);
