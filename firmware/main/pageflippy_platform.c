// ESP32Pageflippy: Bluepad32-Plattform. Liest Controller (DualSense, Fire TV Remote, ...)
// und schickt "vor" / "zurueck" als Pfeiltasten an den Tolino.

#include <stdio.h>
#include <string.h>

#include <btstack_run_loop.h>
#include <esp_console.h>
#include <hci.h>
#include <freertos/FreeRTOS.h>
#include <uni.h>

#include "pageflippy_config.h"
#include "pageflippy_lang.h"
#include "pageflippy_hog.h"
#include "pageflippy_status.h"

// Zustand pro Controller, liegt in d->platform_data.
typedef struct {
    bool next_pressed;
    bool prev_pressed;
    uint32_t last_mask;
    bool connected;
    // Touchpad (virtuelle Maus des DualSense): aufsummierte Bewegung
    int32_t swipe_x;
    uint32_t swipe_last_ms;
    bool swipe_done;
    // Belegen am Controller und Reset
    bool learn_done;
    uint32_t reset_since_ms;
    bool reset_done;
} pageflippy_instance_t;

// Steuerkreuz unten / oben halten + Taste: Taste blaettert ab jetzt zurueck / vor.
#define LEARN_MODIFIERS (BIT(INPUT_DPAD_UP) | BIT(INPUT_DPAD_DOWN))
// R1 + L1 + Kreuz so lange halten, um die Belegung zurueckzusetzen.
#define RESET_HOLD_MS 5000
#define RESET_COMBO (BIT(INPUT_R1) | BIT(INPUT_L1) | BIT(INPUT_A))

// Touchpad-Breite ist 1920; ab dieser Strecke zaehlt eine Bewegung als Wischen.
#define SWIPE_DISTANCE 350
// Ohne Bewegung fuer diese Zeit beginnt ein neues Wischen.
#define SWIPE_RESET_MS 250

static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static pageflippy_status_t status;

// Die Controller-Suche belegt das Funkmodul fast vollstaendig und laesst dem WLAN kaum Zeit.
// Deshalb wird nur fuer kurze Zeit gesucht: nach dem Start, per Weboberflaeche oder BOOT-Taste.
// Bereits gekoppelte Controller wie der DualSense verbinden sich auch ohne Suche von selbst.
#define PAIRING_WINDOW_MS (60 * 1000)
static btstack_timer_source_t pairing_timer;
static uint32_t pairing_end_ms;

_Static_assert(sizeof(pageflippy_instance_t) <= HID_DEVICE_MAX_PLATFORM_DATA, "platform_data too small");

static pageflippy_instance_t* get_instance(uni_hid_device_t* d) {
    return (pageflippy_instance_t*)&d->platform_data[0];
}

static bool keyboard_has_key(const uni_keyboard_t* kb, const uint8_t* keys, size_t count) {
    for (int i = 0; i < UNI_KEYBOARD_PRESSED_KEYS_MAX; i++) {
        for (size_t k = 0; k < count; k++) {
            if (keys[k] != 0 && kb->pressed_keys[i] == keys[k])
                return true;
        }
    }
    return false;
}

static void set_feedback(uni_hid_device_t* d, bool tolino_connected) {
    if (d->report_parser.set_lightbar_color != NULL) {
        // Gruen = Tolino verbunden, Rot = Tolino nicht verbunden
        if (tolino_connected)
            d->report_parser.set_lightbar_color(d, 0x00, 0x60, 0x00);
        else
            d->report_parser.set_lightbar_color(d, 0x60, 0x00, 0x00);
    }
}

static void set_last_input(const char* text) {
    taskENTER_CRITICAL(&status_lock);
    snprintf(status.last_input, sizeof(status.last_input), "%s", text);
    taskEXIT_CRITICAL(&status_lock);
}

static void update_controller_list(void) {
    pageflippy_status_t tmp = {0};
    for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES && tmp.controller_count < PAGEFLIPPY_STATUS_MAX_CONTROLLERS; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || d->parent != NULL || !((pageflippy_instance_t*)&d->platform_data[0])->connected)
            continue;
        snprintf(tmp.controllers[tmp.controller_count++], sizeof(tmp.controllers[0]), "%s",
                 d->name[0] ? d->name : "Controller");
    }
    taskENTER_CRITICAL(&status_lock);
    status.controller_count = tmp.controller_count;
    memcpy(status.controllers, tmp.controllers, sizeof(status.controllers));
    taskEXIT_CRITICAL(&status_lock);
}

//
// Vibrationsmuster: mehrere kraeftige Stoesse hintereinander (laeuft im Bluetooth-Thread).
// DualSense: "weak" = rechter Motor, "strong" = linker Motor.
//
#define PULSE_ON_MS 180
#define PULSE_PERIOD_MS 320
static btstack_timer_source_t pulse_timer;
static int pulse_device = -1;
static int pulse_left;
static uint8_t pulse_weak, pulse_strong;

static void pulse_step(btstack_timer_source_t* ts) {
    ARG_UNUSED(ts);
    uni_hid_device_t* d = pulse_device >= 0 ? uni_hid_device_get_instance_for_idx(pulse_device) : NULL;
    if (d == NULL || pulse_left <= 0 || d->report_parser.play_dual_rumble == NULL)
        return;
    d->report_parser.play_dual_rumble(d, 0, PULSE_ON_MS, pulse_weak, pulse_strong);
    if (--pulse_left > 0) {
        btstack_run_loop_set_timer(&pulse_timer, PULSE_PERIOD_MS);
        btstack_run_loop_add_timer(&pulse_timer);
    }
}

static void rumble_pulses(uni_hid_device_t* d, int count, uint8_t weak, uint8_t strong) {
    if (d->parent != NULL)
        d = d->parent;
    btstack_run_loop_remove_timer(&pulse_timer);
    pulse_device = uni_hid_device_get_idx_for_instance(d);
    pulse_left = count;
    pulse_weak = weak;
    pulse_strong = strong;
    btstack_run_loop_set_timer_handler(&pulse_timer, pulse_step);
    pulse_step(&pulse_timer);
}

//
// Controller ausschalten, wenn der Tolino zu lange weg ist (laeuft im Bluetooth-Thread).
//
#define IDLE_CHECK_MS 5000
static btstack_timer_source_t idle_timer;
static uint32_t idle_since_ms;

static void idle_check(btstack_timer_source_t* ts) {
    uint32_t now = btstack_run_loop_get_time_ms();
    uint16_t limit_s = pageflippy_config.idle_off_s;

    if (pageflippy_hog_is_connected() || limit_s == 0) {
        idle_since_ms = now;
    } else if (now - idle_since_ms >= (uint32_t)limit_s * 1000) {
        for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
            uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
            if (d == NULL || d->parent != NULL || !get_instance(d)->connected)
                continue;
            logi(T("pageflippy: Tolino seit %u s nicht verbunden, schalte %s ab\n", "pageflippy: Tolino not connected for %u s, turning off %s\n"), limit_s, d->name);
            // Trennt der ESP32 die Verbindung, schaltet sich der DualSense aus.
            uni_bt_disconnect_device_safe(i);
        }
        idle_since_ms = now;
    }
    btstack_run_loop_set_timer(ts, IDLE_CHECK_MS);
    btstack_run_loop_add_timer(ts);
}

static void idle_start(void) {
    idle_since_ms = btstack_run_loop_get_time_ms();
    btstack_run_loop_set_timer_handler(&idle_timer, idle_check);
    btstack_run_loop_set_timer(&idle_timer, IDLE_CHECK_MS);
    btstack_run_loop_add_timer(&idle_timer);
}

// Laeuft im Bluetooth-Thread.
static void send_flip(bool next) {
    uint8_t keycode = next ? pageflippy_config.out_next : pageflippy_config.out_prev;
    logi("pageflippy: %s\n", next ? T("vor", "next") : T("zurueck", "back"));
    pageflippy_hog_send_key(keycode);
    taskENTER_CRITICAL(&status_lock);
    status.flips++;
    taskEXIT_CRITICAL(&status_lock);
}

static void flip(uni_hid_device_t* d, bool next) {
    bool connected = pageflippy_hog_is_connected();
    // Touchpad-Eingaben kommen von einem virtuellen Geraet; Licht und Vibration hat der Controller.
    if (d->parent != NULL)
        d = d->parent;
    send_flip(next);
    set_feedback(d, connected);
    if (connected && pageflippy_config.rumble && d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, 40, 80, 0);
}

static void handle_buttons(uni_hid_device_t* d, bool next, bool prev) {
    pageflippy_instance_t* ins = get_instance(d);

    // Nur bei der Flanke "gedrueckt" blaettern, nicht solange die Taste gehalten wird.
    if (next && !ins->next_pressed)
        flip(d, true);
    if (prev && !ins->prev_pressed)
        flip(d, false);

    ins->next_pressed = next;
    ins->prev_pressed = prev;
}

//
// Controller-Suche (laeuft im Bluetooth-Thread)
//
static void pairing_stop(void) {
    btstack_run_loop_remove_timer(&pairing_timer);
    if (uni_bt_is_scanning()) {
        logi(T("pageflippy: Controller-Suche beendet\n", "pageflippy: controller search stopped\n"));
        uni_bt_stop_scanning_unsafe();
    }
    pairing_end_ms = 0;
}

static void pairing_timeout(btstack_timer_source_t* ts) {
    ARG_UNUSED(ts);
    pairing_stop();
}

static bool bt_off;

static void pairing_start(void) {
    if (bt_off) {
        logi(T("pageflippy: Bluetooth ist aus, Controller-Suche erst nach Neustart\n", "pageflippy: Bluetooth is off, controller search only after restart\n"));
        return;
    }
    logi(T("pageflippy: Controller-Suche fuer %d s\n", "pageflippy: controller search for %d s\n"), PAIRING_WINDOW_MS / 1000);
    uni_bt_start_scanning_and_autoconnect_unsafe();
    btstack_run_loop_remove_timer(&pairing_timer);
    btstack_run_loop_set_timer_handler(&pairing_timer, pairing_timeout);
    btstack_run_loop_set_timer(&pairing_timer, PAIRING_WINDOW_MS);
    btstack_run_loop_add_timer(&pairing_timer);
    pairing_end_ms = btstack_run_loop_get_time_ms() + PAIRING_WINDOW_MS;
}

//
// Plattform-Callbacks
//
static void pageflippy_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    logi("pageflippy: init\n");
    pageflippy_config_load();
}

static void pageflippy_on_init_complete(void) {
    logi(T("pageflippy: Bluetooth bereit\n", "pageflippy: Bluetooth ready\n"));

    // BLE-Tastatur fuer den Tolino starten
    pageflippy_hog_init();

    // Gekoppelte Controller duerfen sich jederzeit von selbst verbinden.
    // Gespeicherte Kopplungen (auch die zum Tolino) bleiben erhalten.
    uni_bt_allow_incoming_connections(true);

    // DualSense-Touchpad als eigenes (virtuelles) Maus-Geraet melden lassen.
    uni_virtual_device_set_enabled(true);
    pairing_start();
    idle_start();
}

static uni_error_t pageflippy_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    ARG_UNUSED(addr);
    ARG_UNUSED(cod);
    ARG_UNUSED(rssi);
    logi(T("pageflippy: gefunden: %s\n", "pageflippy: found: %s\n"), name ? name : "(ohne Name)");
    // Alles zulassen, auch Tastaturen und Fernbedienungen (z. B. Fire TV Remote).
    return UNI_ERROR_SUCCESS;
}

static void pageflippy_on_device_connected(uni_hid_device_t* d) {
    logi(T("pageflippy: Controller verbunden: %s\n", "pageflippy: controller connected: %s\n"), d->name);
}

static void pageflippy_on_device_disconnected(uni_hid_device_t* d) {
    logi(T("pageflippy: Controller getrennt: %s\n", "pageflippy: controller disconnected: %s\n"), d->name);
    get_instance(d)->connected = false;
    update_controller_list();
}

static uni_error_t pageflippy_on_device_ready(uni_hid_device_t* d) {
    logi(T("pageflippy: Controller bereit: %s\n", "pageflippy: controller ready: %s\n"), d->name);
    memset(get_instance(d), 0, sizeof(pageflippy_instance_t));
    get_instance(d)->connected = true;
    update_controller_list();
    // Frisch eingeschalteter Controller bekommt die volle Wartezeit auf den Tolino.
    idle_since_ms = btstack_run_loop_get_time_ms();
    // Controller ist da: Suche beenden, damit das WLAN wieder Funkzeit bekommt.
    pairing_stop();

    if (d->parent != NULL)
        return UNI_ERROR_SUCCESS;  // Touchpad des DualSense
    if (d->report_parser.set_player_leds != NULL)
        d->report_parser.set_player_leds(d, 0x01);
    set_feedback(d, pageflippy_hog_is_connected());
    if (d->report_parser.play_dual_rumble != NULL)
        d->report_parser.play_dual_rumble(d, 0, 150, 128, 40);
    return UNI_ERROR_SUCCESS;
}

// Zeigt neu gedrueckte Eingaben in der Weboberflaeche an.
static void report_mask(pageflippy_instance_t* ins, uint32_t mask) {
    uint32_t pressed = mask & ~ins->last_mask;
    ins->last_mask = mask;
    for (int i = 0; i < INPUT_COUNT; i++) {
        if (pressed & BIT(i))
            set_last_input(pageflippy_input_name((pageflippy_input_t)i));
    }
}

// Belegen am Controller und Reset. Gibt true zurueck, solange nicht geblaettert werden soll.
// "held" sind die Steuerkreuz-Tasten des Controllers (beim Touchpad: die des DualSense).
static bool handle_learn(uni_hid_device_t* d, pageflippy_instance_t* ins, uint32_t mask, uint32_t pressed, uint32_t held) {
    uint32_t mods = held & LEARN_MODIFIERS;

    // Reset: R1 + L1 + Kreuz 5 s halten
    bool reset_combo = (mask & RESET_COMBO) == RESET_COMBO;
    if (reset_combo) {
        uint32_t now = btstack_run_loop_get_time_ms();
        if (ins->reset_since_ms == 0)
            ins->reset_since_ms = now;
        if (!ins->reset_done && now - ins->reset_since_ms >= RESET_HOLD_MS) {
            ins->reset_done = true;
            pageflippy_config_reset_mapping();
            pageflippy_config_save();
            logi(T("pageflippy: Belegung auf Standard zurueckgesetzt\n", "pageflippy: mapping reset to defaults\n"));
            set_last_input(T("Belegung zurueckgesetzt", "Mapping reset"));
            rumble_pulses(d, 3, 0xff, 0xff);
        }
        return true;
    }
    ins->reset_since_ms = 0;
    ins->reset_done = false;

    if (mods == 0 || mods == LEARN_MODIFIERS) {
        ins->learn_done = false;
        return mods != 0;
    }

    // Genau eine Richtung gehalten: die erste neu gedrueckte Taste wird belegt.
    pressed &= ~LEARN_MODIFIERS;
    if (pressed != 0 && !ins->learn_done) {
        bool next = mods == BIT(INPUT_DPAD_UP);
        int input = __builtin_ctz(pressed);
        pageflippy_config_assign((pageflippy_input_t)input, next);
        pageflippy_config_save();
        ins->learn_done = true;
        char text[64];
        snprintf(text, sizeof(text), "%s -> %s", pageflippy_input_name((pageflippy_input_t)input),
                 next ? T("vor", "next") : T("zurueck", "back"));
        set_last_input(text);
        logi(T("pageflippy: belegt: %s\n", "pageflippy: assigned: %s\n"), text);
        // vor = rechter Motor, zurueck = linker Motor, je 2x kraeftig
        rumble_pulses(d, 2, next ? 0xff : 0x00, next ? 0x00 : 0xff);
    }
    if (pressed == 0)
        ins->learn_done = false;
    return true;
}

// Blaettern, ausser waehrend Belegen/Reset: dann nur den Tastenzustand merken, damit beim
// Loslassen des Steuerkreuzes nicht nachtraeglich geblaettert wird.
static void handle_mask(uni_hid_device_t* d, pageflippy_instance_t* ins, uint32_t mask, uint32_t held) {
    const pageflippy_config_t* cfg = &pageflippy_config;
    uint32_t pressed = mask & ~ins->last_mask;
    report_mask(ins, mask);
    bool next = (mask & cfg->next_mask) != 0;
    bool prev = (mask & cfg->prev_mask) != 0;
    if (handle_learn(d, ins, mask, pressed, held)) {
        ins->next_pressed = next;
        ins->prev_pressed = prev;
        return;
    }
    handle_buttons(d, next, prev);
}

static void pageflippy_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    const pageflippy_config_t* cfg = &pageflippy_config;
    pageflippy_instance_t* ins = get_instance(d);
    char text[48];

    switch (ctl->klass) {
        case UNI_CONTROLLER_CLASS_GAMEPAD: {
            uint32_t mask = pageflippy_gamepad_mask(&ctl->gamepad);
            handle_mask(d, ins, mask, mask);
            break;
        }
        case UNI_CONTROLLER_CLASS_MOUSE: {
            // DualSense-Touchpad: Wischen und Druecken
            const uni_mouse_t* ms = &ctl->mouse;
            uint32_t now = btstack_run_loop_get_time_ms();
            uint32_t mask = 0;

            if (ms->delta_x != 0 || ms->delta_y != 0) {
                if (now - ins->swipe_last_ms > SWIPE_RESET_MS) {
                    ins->swipe_x = 0;
                    ins->swipe_done = false;
                }
                ins->swipe_last_ms = now;
                ins->swipe_x += ms->delta_x;
                if (!ins->swipe_done && ins->swipe_x <= -SWIPE_DISTANCE) {
                    mask |= BIT(INPUT_TOUCH_SWIPE_LEFT);
                    ins->swipe_done = true;
                } else if (!ins->swipe_done && ins->swipe_x >= SWIPE_DISTANCE) {
                    mask |= BIT(INPUT_TOUCH_SWIPE_RIGHT);
                    ins->swipe_done = true;
                }
            }
            if (ms->buttons & UNI_MOUSE_BUTTON_LEFT)
                mask |= BIT(INPUT_TOUCH_CLICK);
            if (ms->buttons & UNI_MOUSE_BUTTON_RIGHT)
                mask |= BIT(INPUT_TOUCH_CLICK_RIGHT);

            // Steuerkreuz-Zustand kommt vom DualSense selbst (Elterngeraet).
            uint32_t held = d->parent != NULL ? get_instance(d->parent)->last_mask : 0;
            handle_mask(d, ins, mask, held);
            break;
        }
        case UNI_CONTROLLER_CLASS_KEYBOARD: {
            // Tastaturen und Fernbedienungen (z. B. Fire TV Remote)
            const uni_keyboard_t* kb = &ctl->keyboard;
            for (int i = 0; i < UNI_KEYBOARD_PRESSED_KEYS_MAX; i++) {
                if (kb->pressed_keys[i] != 0) {
                    snprintf(text, sizeof(text), T("Taste 0x%02X", "Key 0x%02X"), kb->pressed_keys[i]);
                    set_last_input(text);
                }
            }
            bool next = keyboard_has_key(kb, cfg->next_keys, PAGEFLIPPY_MAX_KEYS);
            bool prev = keyboard_has_key(kb, cfg->prev_keys, PAGEFLIPPY_MAX_KEYS);
            handle_buttons(d, next, prev);
            break;
        }
        default:
            break;
    }
}

static const uni_property_t* pageflippy_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void pageflippy_on_oob_event(uni_platform_oob_event_t event, void* data) {
    ARG_UNUSED(event);
    ARG_UNUSED(data);
}

//
// Zugriff aus dem Webserver
//
void pageflippy_status_get(pageflippy_status_t* out) {
    taskENTER_CRITICAL(&status_lock);
    *out = status;
    taskEXIT_CRITICAL(&status_lock);
    out->tolino_connected = pageflippy_hog_is_connected();

    uint32_t end = pairing_end_ms;
    uint32_t now = btstack_run_loop_get_time_ms();
    out->pairing_left_s = (end != 0 && end > now) ? (end - now + 999) / 1000 : 0;
    out->bt_off = bt_off;
}

static void bt_off_cb(void* context) {
    ARG_UNUSED(context);
    if (bt_off)
        return;
    pairing_stop();
    bt_off = true;
    logi(T("pageflippy: Bluetooth aus bis zum naechsten Neustart\n", "pageflippy: Bluetooth off until next restart\n"));
    // Trennt Controller und Tolino und schaltet das Funkteil ab.
    hci_power_control(HCI_POWER_OFF);
}

void pageflippy_request_bt_off(void) {
    static btstack_context_callback_registration_t reg = {.callback = bt_off_cb};
    btstack_run_loop_execute_on_main_thread(&reg);
}

static int cmd_bt_off(int argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    pageflippy_request_bt_off();
    return 0;
}

static void reset_map_cb(void* context) {
    ARG_UNUSED(context);
    pageflippy_config_reset_mapping();
    pageflippy_config_save();
    logi(T("pageflippy: Belegung auf Standard zurueckgesetzt\n", "pageflippy: mapping reset to defaults\n"));
}

static int cmd_reset_map(int argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    static btstack_context_callback_registration_t reg = {.callback = reset_map_cb};
    btstack_run_loop_execute_on_main_thread(&reg);
    return 0;
}

static void pageflippy_register_console_cmds(void) {
    static const esp_console_cmd_t reset_map_cmd = {
        .command = "reset_map",
        .help = T("Tastenbelegung auf Standard zuruecksetzen", "Reset button mapping to defaults"),
        .func = &cmd_reset_map,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reset_map_cmd));
    static const esp_console_cmd_t bt_off_cmd = {
        .command = "bt_off",
        .help = T("Bluetooth bis zum naechsten Neustart ausschalten (WLAN wird schneller)", "Turn Bluetooth off until next restart (Wi-Fi gets faster)"),
        .func = &cmd_bt_off,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&bt_off_cmd));
}

static void pairing_cb(void* context) {
    ARG_UNUSED(context);
    pairing_start();
}

void pageflippy_request_pairing(void) {
    static btstack_context_callback_registration_t reg = {.callback = pairing_cb};
    btstack_run_loop_execute_on_main_thread(&reg);
}

static void flip_next_cb(void* context) {
    ARG_UNUSED(context);
    send_flip(true);
}

static void flip_prev_cb(void* context) {
    ARG_UNUSED(context);
    send_flip(false);
}

void pageflippy_request_flip(bool next) {
    static btstack_context_callback_registration_t next_reg = {.callback = flip_next_cb};
    static btstack_context_callback_registration_t prev_reg = {.callback = flip_prev_cb};
    // Eine Registrierung darf nicht doppelt eingereiht werden; BTstack ignoriert das.
    btstack_run_loop_execute_on_main_thread(next ? &next_reg : &prev_reg);
}

struct uni_platform* get_my_platform(void) {
    static struct uni_platform plat = {
        .name = "ESP32Pageflippy",
        .init = pageflippy_init,
        .on_init_complete = pageflippy_on_init_complete,
        .on_device_discovered = pageflippy_on_device_discovered,
        .on_device_connected = pageflippy_on_device_connected,
        .on_device_disconnected = pageflippy_on_device_disconnected,
        .on_device_ready = pageflippy_on_device_ready,
        .on_oob_event = pageflippy_on_oob_event,
        .on_controller_data = pageflippy_on_controller_data,
        .get_property = pageflippy_get_property,
        .register_console_cmds = pageflippy_register_console_cmds,
    };
    return &plat;
}
