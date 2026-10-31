// ESP32Pageflippy: zwei kapazitive Touch-Tasten am ESP32 (GPIO32 = vor, GPIO33 = zurueck).
#pragma once

#include <stdint.h>

void pageflippy_touch_start(void);
// Aktuelle (gefilterte) Messwerte, 0 = aus. Kleiner Wert = beruehrt.
void pageflippy_touch_values(uint16_t* next, uint16_t* prev);
