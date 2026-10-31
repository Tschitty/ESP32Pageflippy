// ESP32Pageflippy: BLE-HID-Tastatur ("HID over GATT") fuer den Tolino.
// Aufbau wie BTstack example/hog_keyboard_demo.c, aber neben Bluepad32 im selben Stack.

#include "pageflippy_hog.h"
#include "pageflippy_lang.h"

#include <inttypes.h>
#include <string.h>

#include <btstack.h>
#include <uni.h>

#include "pageflippy_hog.gatt.h"

#define DEVICE_NAME "ESP32Pageflippy"

static const uint8_t adv_data[] = {
    // Flags: general discoverable, BR/EDR not supported
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Name
    0x10, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'E', 'S', 'P', '3', '2', 'P', 'a', 'g', 'e', 'f', 'l', 'i', 'p', 'p', 'y',
    // 16-bit Service UUIDs: HID
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xff, ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    // Appearance: Keyboard
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC1, 0x03,
};
_Static_assert(sizeof(adv_data) <= 31, "adv_data too big");

// Standard-Tastatur mit Report-ID 1: Modifier, reserviert, 6 Tasten; 5 LEDs als Ausgabe.
static const uint8_t hid_descriptor[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
};

static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;
static hci_con_handle_t peer_handle = HCI_CON_HANDLE_INVALID;

static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static uint8_t protocol_mode = 1;  // 0 = Boot, 1 = Report

// Kleine Warteschlange mit Tastencodes; 0 = alle Tasten losgelassen.
#define QUEUE_SIZE 16
static uint8_t queue[QUEUE_SIZE];
static uint8_t queue_head;
static uint8_t queue_tail;

static bool queue_empty(void) {
    return queue_head == queue_tail;
}

static void queue_push(uint8_t keycode) {
    uint8_t next = (queue_tail + 1) % QUEUE_SIZE;
    if (next == queue_head) {
        loge(T("pageflippy: Warteschlange voll, Taste verworfen\n", "pageflippy: queue full, key dropped\n"));
        return;
    }
    queue[queue_tail] = keycode;
    queue_tail = next;
}

static void send_next_report(void) {
    if (queue_empty() || con_handle == HCI_CON_HANDLE_INVALID)
        return;

    uint8_t keycode = queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_SIZE;

    uint8_t report[] = {0, 0, keycode, 0, 0, 0, 0, 0};
    if (protocol_mode == 0)
        hids_device_send_boot_keyboard_input_report(con_handle, report, sizeof(report));
    else
        hids_device_send_input_report(con_handle, report, sizeof(report));

    if (!queue_empty())
        hids_device_request_can_send_now_event(con_handle);
}

// Laengeres Verbindungsintervall (30-60 ms, Latenz 4): laesst mehr Funkzeit fuer WLAN und
// Controller. Tastendruecke kommen trotzdem beim naechsten Intervall an.
static void relax_connection(void) {
    gap_request_connection_parameter_update(con_handle, 24, 48, 4, 600);
}

static bool is_peripheral_le_connection(hci_con_handle_t handle) {
    return gap_get_connection_type(handle) == GAP_CONNECTION_LE && gap_get_role(handle) == HCI_ROLE_SLAVE;
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_LE_META:
            // Eingehende LE-Verbindung, in der wir Peripheral sind: das ist der Tolino (oder ein Handy).
            if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE &&
                hci_subevent_le_connection_complete_get_status(packet) == ERROR_CODE_SUCCESS &&
                hci_subevent_le_connection_complete_get_role(packet) == HCI_ROLE_SLAVE) {
                bd_addr_t addr;
                hci_subevent_le_connection_complete_get_peer_address(packet, addr);
                peer_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                logi(T("pageflippy: LE-Verbindung von %s\n", "pageflippy: LE connection from %s\n"), bd_addr_to_str(addr));
            }
            break;
        case SM_EVENT_PAIRING_STARTED:
            logi(T("pageflippy: Pairing gestartet\n", "pageflippy: pairing started\n"));
            break;
        case SM_EVENT_REENCRYPTION_COMPLETE:
            logi(T("pageflippy: Verschluesselung mit gespeicherter Kopplung, Status 0x%02x%s\n", "pageflippy: encryption with stored pairing, status 0x%02x%s\n"),
                 sm_event_reencryption_complete_get_status(packet),
                 sm_event_reencryption_complete_get_status(packet) ? T(" (Kopplung passt nicht)", " (pairing does not match)") : "");
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if (hci_event_disconnection_complete_get_connection_handle(packet) == peer_handle) {
                uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
                logi(T("pageflippy: LE-Verbindung getrennt, Grund 0x%02x%s\n", "pageflippy: LE connection closed, reason 0x%02x%s\n"), reason,
                     reason == 0x06 ? T(" (Schluessel fehlt: am Tolino Kopplung entfernen und neu koppeln)", " (key missing: remove the pairing on the Tolino and pair again)") : "");
                peer_handle = HCI_CON_HANDLE_INVALID;
            }
            if (hci_event_disconnection_complete_get_connection_handle(packet) == con_handle) {
                logi(T("pageflippy: Tolino getrennt\n", "pageflippy: Tolino disconnected\n"));
                con_handle = HCI_CON_HANDLE_INVALID;
                queue_head = queue_tail = 0;
            }
            break;

        // Bluepad32 ignoriert SM-Events von Verbindungen, in denen wir Peripheral sind
        // (siehe patches/), deshalb bestaetigen wir das Pairing hier selbst.
        case SM_EVENT_JUST_WORKS_REQUEST:
            if (is_peripheral_le_connection(sm_event_just_works_request_get_handle(packet))) {
                logi(T("pageflippy: Pairing (Just Works) bestaetigt\n", "pageflippy: pairing (Just Works) confirmed\n"));
                sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            }
            break;
        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            if (is_peripheral_le_connection(sm_event_numeric_comparison_request_get_handle(packet))) {
                logi(T("pageflippy: Pairing (Numeric Comparison) bestaetigt\n", "pageflippy: pairing (Numeric Comparison) confirmed\n"));
                sm_numeric_comparison_confirm(sm_event_numeric_comparison_request_get_handle(packet));
            }
            break;
        case SM_EVENT_PAIRING_COMPLETE:
            if (is_peripheral_le_connection(sm_event_pairing_complete_get_handle(packet)))
                logi(T("pageflippy: Pairing abgeschlossen, Status 0x%02x\n", "pageflippy: pairing complete, status 0x%02x\n"),
                     sm_event_pairing_complete_get_status(packet));
            break;

        case HCI_EVENT_HIDS_META:
            switch (hci_event_hids_meta_get_subevent_code(packet)) {
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
                    con_handle = hids_subevent_input_report_enable_get_con_handle(packet);
                    relax_connection();
                    logi(T("pageflippy: Tolino verbunden (Report-Modus)\n", "pageflippy: Tolino connected (report mode)\n"));
                    break;
                case HIDS_SUBEVENT_BOOT_KEYBOARD_INPUT_REPORT_ENABLE:
                    con_handle = hids_subevent_boot_keyboard_input_report_enable_get_con_handle(packet);
                    relax_connection();
                    logi(T("pageflippy: Tolino verbunden (Boot-Modus)\n", "pageflippy: Tolino connected (boot mode)\n"));
                    break;
                case HIDS_SUBEVENT_PROTOCOL_MODE:
                    protocol_mode = hids_subevent_protocol_mode_get_protocol_mode(packet);
                    break;
                case HIDS_SUBEVENT_CAN_SEND_NOW:
                    send_next_report();
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

void pageflippy_hog_init(void) {
    // SM wurde bereits von Bluepad32 initialisiert (Bonding, ohne Ein-/Ausgabe).
    att_server_init(profile_data, NULL, NULL);

    battery_service_server_init(100);
    device_information_service_server_init();
    device_information_service_server_set_manufacturer_name(DEVICE_NAME);
    device_information_service_server_set_model_number(DEVICE_NAME " HID Keyboard");
    device_information_service_server_set_pnp_id(0x02, 0xE502, 0xA111, 0x0100);

    hids_device_init(0, hid_descriptor, sizeof(hid_descriptor));

    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    sm_event_callback_registration.callback = &packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);
    hids_device_register_packet_handler(packet_handler);

    // 100 ms Werbeintervall, damit BR/EDR fuer den Controller genug Funkzeit bleibt.
    bd_addr_t null_addr;
    memset(null_addr, 0, sizeof(null_addr));
    gap_advertisements_set_params(0x00A0, 0x00A0, 0 /* ADV_IND */, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(sizeof(adv_data), (uint8_t*)adv_data);
    gap_advertisements_enable(1);

    logi(T("pageflippy: BLE-Tastatur \"%s\" bereit\n", "pageflippy: BLE keyboard \"%s\" ready\n"), DEVICE_NAME);
}

void pageflippy_hog_send_key(uint8_t keycode) {
    if (con_handle == HCI_CON_HANDLE_INVALID) {
        logi(T("pageflippy: Tolino nicht verbunden, Taste 0x%02x ignoriert\n", "pageflippy: Tolino not connected, key 0x%02x ignored\n"), keycode);
        return;
    }
    queue_push(keycode);
    queue_push(0);
    hids_device_request_can_send_now_event(con_handle);
}

bool pageflippy_hog_is_connected(void) {
    return con_handle != HCI_CON_HANDLE_INVALID;
}
