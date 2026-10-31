// ESP32Pageflippy: Tastenbelegung und WLAN-Einstellungen (in NVS gespeichert).

#include "pageflippy_config.h"
#include "pageflippy_lang.h"

#include <stddef.h>
#include <string.h>

#include <nvs.h>
#include <uni.h>

#define NVS_NAMESPACE "pageflippy"
#define CONFIG_VERSION 2

// Ab diesem Wert (Bereich -512..511) zaehlt ein Stick als ausgelenkt.
#define STICK_THRESHOLD 300

pageflippy_config_t pageflippy_config;
static volatile uint32_t config_rev;

uint32_t pageflippy_config_revision(void) {
    return config_rev;
}

static const char* const input_names[INPUT_COUNT] = {
    [INPUT_DPAD_UP] = T("Steuerkreuz hoch", "D-pad up"),
    [INPUT_DPAD_DOWN] = T("Steuerkreuz runter", "D-pad down"),
    [INPUT_DPAD_LEFT] = T("Steuerkreuz links", "D-pad left"),
    [INPUT_DPAD_RIGHT] = T("Steuerkreuz rechts", "D-pad right"),
    [INPUT_A] = T("Kreuz (A)", "Cross (A)"),
    [INPUT_B] = T("Kreis (B)", "Circle (B)"),
    [INPUT_X] = T("Quadrat (X)", "Square (X)"),
    [INPUT_Y] = T("Dreieck (Y)", "Triangle (Y)"),
    [INPUT_L1] = "L1",
    [INPUT_R1] = "R1",
    [INPUT_L2] = "L2",
    [INPUT_R2] = "R2",
    [INPUT_L3] = T("L3 (Stick links druecken)", "L3 (press left stick)"),
    [INPUT_R3] = T("R3 (Stick rechts druecken)", "R3 (press right stick)"),
    [INPUT_SYSTEM] = "PS / Home",
    [INPUT_SELECT] = "Create / Select",
    [INPUT_START] = "Options / Start",
    [INPUT_CAPTURE] = "Mute / Capture",
    [INPUT_STICK_L_LEFT] = T("Linker Stick links", "Left stick left"),
    [INPUT_STICK_L_RIGHT] = T("Linker Stick rechts", "Left stick right"),
    [INPUT_STICK_R_LEFT] = T("Rechter Stick links", "Right stick left"),
    [INPUT_STICK_R_RIGHT] = T("Rechter Stick rechts", "Right stick right"),
    [INPUT_TOUCH_SWIPE_LEFT] = T("Touchpad nach links wischen", "Touchpad swipe left"),
    [INPUT_TOUCH_SWIPE_RIGHT] = T("Touchpad nach rechts wischen", "Touchpad swipe right"),
    [INPUT_TOUCH_CLICK] = T("Touchpad druecken (linke 3/4)", "Touchpad press (left 3/4)"),
    [INPUT_TOUCH_CLICK_RIGHT] = T("Touchpad druecken (rechtes 1/4)", "Touchpad press (right 1/4)"),
};

const char* pageflippy_input_name(pageflippy_input_t input) {
    if (input >= INPUT_COUNT)
        return "?";
    return input_names[input];
}

void pageflippy_config_reset_mapping(void) {
    pageflippy_config.next_mask =
        BIT(INPUT_DPAD_RIGHT) | BIT(INPUT_R1) | BIT(INPUT_R2) | BIT(INPUT_A) | BIT(INPUT_STICK_L_RIGHT) |
                                BIT(INPUT_TOUCH_SWIPE_LEFT) | BIT(INPUT_TOUCH_CLICK);
    pageflippy_config.prev_mask =
        BIT(INPUT_DPAD_LEFT) | BIT(INPUT_L1) | BIT(INPUT_L2) | BIT(INPUT_X) | BIT(INPUT_STICK_L_LEFT) |
                                BIT(INPUT_TOUCH_SWIPE_RIGHT) | BIT(INPUT_TOUCH_CLICK_RIGHT);
    // Pfeil rechts, Bild ab, Leertaste / Pfeil links, Bild auf
    pageflippy_config.next_keys[0] = 0x4F;
    pageflippy_config.next_keys[1] = 0x4E;
    pageflippy_config.next_keys[2] = 0x2C;
    pageflippy_config.prev_keys[0] = 0x50;
    pageflippy_config.prev_keys[1] = 0x4B;
}

void pageflippy_config_reset(void) {
    memset(&pageflippy_config, 0, sizeof(pageflippy_config));
    pageflippy_config_reset_mapping();
    // So blaettert eMote am Tolino: Pfeil rechts / Pfeil links
    pageflippy_config.out_next = 0x4F;
    pageflippy_config.out_prev = 0x50;
    pageflippy_config.rumble = true;
    pageflippy_config.idle_off_s = 120;
}

void pageflippy_config_assign(pageflippy_input_t input, bool next) {
    if (input >= INPUT_COUNT)
        return;
    if (next) {
        pageflippy_config.next_mask |= BIT(input);
        pageflippy_config.prev_mask &= ~BIT(input);
    } else {
        pageflippy_config.prev_mask |= BIT(input);
        pageflippy_config.next_mask &= ~BIT(input);
    }
}

void pageflippy_config_load(void) {
    pageflippy_config_reset();

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
        return;

    uint8_t version = 0;
    pageflippy_config_t stored = pageflippy_config;
    size_t len = sizeof(stored);
    // Aeltere, kuerzere Blobs (ohne idle_off_s) werden ueber die Standardwerte gelegt.
    if (nvs_get_u8(h, "cfg_ver", &version) == ESP_OK && version == CONFIG_VERSION &&
        nvs_get_blob(h, "cfg", &stored, &len) == ESP_OK && len >= offsetof(pageflippy_config_t, idle_off_s)) {
        pageflippy_config = stored;
        logi(T("pageflippy: Belegung aus NVS geladen\n", "pageflippy: mapping loaded from NVS\n"));
    }
    nvs_close(h);
}

void pageflippy_config_save(void) {
    config_rev++;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        loge(T("pageflippy: NVS konnte nicht geoeffnet werden\n", "pageflippy: could not open NVS\n"));
        return;
    }
    nvs_set_u8(h, "cfg_ver", CONFIG_VERSION);
    nvs_set_blob(h, "cfg", &pageflippy_config, sizeof(pageflippy_config));
    nvs_commit(h);
    nvs_close(h);
}

void pageflippy_wifi_config_load(pageflippy_wifi_config_t* out) {
    memset(out, 0, sizeof(*out));
    strcpy(out->ap_pass, "pageflippy");

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
        return;
    size_t len = sizeof(out->sta_ssid);
    nvs_get_str(h, "sta_ssid", out->sta_ssid, &len);
    len = sizeof(out->sta_pass);
    nvs_get_str(h, "sta_pass", out->sta_pass, &len);
    len = sizeof(out->ap_pass);
    if (nvs_get_str(h, "ap_pass", out->ap_pass, &len) != ESP_OK || strlen(out->ap_pass) < 8)
        strcpy(out->ap_pass, "pageflippy");
    nvs_close(h);
}

void pageflippy_wifi_config_save(const pageflippy_wifi_config_t* cfg) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        loge(T("pageflippy: NVS konnte nicht geoeffnet werden\n", "pageflippy: could not open NVS\n"));
        return;
    }
    nvs_set_str(h, "sta_ssid", cfg->sta_ssid);
    nvs_set_str(h, "sta_pass", cfg->sta_pass);
    nvs_set_str(h, "ap_pass", cfg->ap_pass);
    nvs_commit(h);
    nvs_close(h);
}

uint32_t pageflippy_gamepad_mask(const uni_gamepad_t* gp) {
    uint32_t m = 0;
    if (gp->dpad & DPAD_UP)
        m |= BIT(INPUT_DPAD_UP);
    if (gp->dpad & DPAD_DOWN)
        m |= BIT(INPUT_DPAD_DOWN);
    if (gp->dpad & DPAD_LEFT)
        m |= BIT(INPUT_DPAD_LEFT);
    if (gp->dpad & DPAD_RIGHT)
        m |= BIT(INPUT_DPAD_RIGHT);
    if (gp->buttons & BUTTON_A)
        m |= BIT(INPUT_A);
    if (gp->buttons & BUTTON_B)
        m |= BIT(INPUT_B);
    if (gp->buttons & BUTTON_X)
        m |= BIT(INPUT_X);
    if (gp->buttons & BUTTON_Y)
        m |= BIT(INPUT_Y);
    if (gp->buttons & BUTTON_SHOULDER_L)
        m |= BIT(INPUT_L1);
    if (gp->buttons & BUTTON_SHOULDER_R)
        m |= BIT(INPUT_R1);
    if (gp->buttons & BUTTON_TRIGGER_L)
        m |= BIT(INPUT_L2);
    if (gp->buttons & BUTTON_TRIGGER_R)
        m |= BIT(INPUT_R2);
    if (gp->buttons & BUTTON_THUMB_L)
        m |= BIT(INPUT_L3);
    if (gp->buttons & BUTTON_THUMB_R)
        m |= BIT(INPUT_R3);
    if (gp->misc_buttons & MISC_BUTTON_SYSTEM)
        m |= BIT(INPUT_SYSTEM);
    if (gp->misc_buttons & MISC_BUTTON_SELECT)
        m |= BIT(INPUT_SELECT);
    if (gp->misc_buttons & MISC_BUTTON_START)
        m |= BIT(INPUT_START);
    if (gp->misc_buttons & MISC_BUTTON_CAPTURE)
        m |= BIT(INPUT_CAPTURE);
    if (gp->axis_x < -STICK_THRESHOLD)
        m |= BIT(INPUT_STICK_L_LEFT);
    if (gp->axis_x > STICK_THRESHOLD)
        m |= BIT(INPUT_STICK_L_RIGHT);
    if (gp->axis_rx < -STICK_THRESHOLD)
        m |= BIT(INPUT_STICK_R_LEFT);
    if (gp->axis_rx > STICK_THRESHOLD)
        m |= BIT(INPUT_STICK_R_RIGHT);
    return m;
}
