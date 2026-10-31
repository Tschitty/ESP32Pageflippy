// ESP32Pageflippy: BLE-HID-Tastatur ("HID over GATT") fuer den Tolino.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// HID-Tastencodes, die der Tolino zum Blaettern versteht (gemessen an eMote).
#define PAGEFLIPPY_KEY_NEXT 0x4F  // Pfeil rechts
#define PAGEFLIPPY_KEY_PREV 0x50  // Pfeil links

// Startet GATT-Server, HID-Dienst und Werbung. Aus dem BTstack-Thread aufrufen,
// nachdem Bluepad32 fertig initialisiert ist.
void pageflippy_hog_init(void);

// Sendet einen Tastendruck (Druecken + Loslassen). Aus dem BTstack-Thread aufrufen.
void pageflippy_hog_send_key(uint8_t keycode);

bool pageflippy_hog_is_connected(void);
