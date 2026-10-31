// ESP32Pageflippy: zwei kapazitive Touch-Tasten am ESP32.
//
// An GPIO32 (T9) und GPIO33 (T8) ein Stueck Draht, Alufolie oder Kupferband anschliessen.
// Beruehren laesst den Messwert fallen; faellt er unter 70 % des Ruhewerts, zaehlt das als Druck.
// Der Ruhewert wird beim Start und danach laufend langsam nachgefuehrt (Temperatur, Feuchte).
// Standardmaessig aus: offene Pins liefern sonst zufaellige Werte.

#include "pageflippy_touch.h"

#include <driver/touch_pad.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <uni.h>

#include "pageflippy_config.h"
#include "pageflippy_lang.h"
#include "pageflippy_status.h"

#define PAD_NEXT TOUCH_PAD_NUM9  // GPIO32
#define PAD_PREV TOUCH_PAD_NUM8  // GPIO33
#define POLL_MS 30
#define TOUCH_PERCENT 70
#define RELEASE_PERCENT 85
// Ab so vielen Messungen am Stueck gilt ein Druck (Entprellung)
#define DEBOUNCE 2

typedef struct {
    touch_pad_t pad;
    bool next;
    uint32_t baseline;
    uint16_t value;
    uint8_t count;
    bool pressed;
} touch_key_t;

static touch_key_t keys[] = {
    {.pad = PAD_NEXT, .next = true},
    {.pad = PAD_PREV, .next = false},
};
static bool started;

void pageflippy_touch_values(uint16_t* next, uint16_t* prev) {
    *next = started ? keys[0].value : 0;
    *prev = started ? keys[1].value : 0;
}

static void poll_key(touch_key_t* k) {
    uint16_t v = 0;
    if (touch_pad_read_filtered(k->pad, &v) != ESP_OK || v == 0)
        return;
    k->value = v;
    if (k->baseline == 0)
        k->baseline = v;

    bool below = v * 100 < k->baseline * TOUCH_PERCENT;
    bool above = v * 100 > k->baseline * RELEASE_PERCENT;

    if (!k->pressed) {
        k->count = below ? k->count + 1 : 0;
        if (k->count >= DEBOUNCE) {
            k->pressed = true;
            k->count = 0;
            if (pageflippy_config.touch_enabled) {
                logi(T("pageflippy: Touch-Taste %s\n", "pageflippy: touch key %s\n"),
                     k->next ? T("vor", "next") : T("zurueck", "back"));
                pageflippy_request_flip(k->next);
            }
        }
        // Ruhewert langsam nachfuehren, solange nicht beruehrt
        if (!below)
            k->baseline = (k->baseline * 63 + v) / 64;
    } else if (above) {
        k->pressed = false;
    }
}

static void touch_task(void* arg) {
    ARG_UNUSED(arg);
    while (true) {
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
            poll_key(&keys[i]);
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void pageflippy_touch_start(void) {
    if (touch_pad_init() != ESP_OK) {
        loge(T("pageflippy: Touch konnte nicht starten\n", "pageflippy: touch could not start\n"));
        return;
    }
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        touch_pad_config(keys[i].pad, 0);
    touch_pad_filter_start(10);
    started = true;
    xTaskCreate(touch_task, "touch", 2560, NULL, 2, NULL);
}
