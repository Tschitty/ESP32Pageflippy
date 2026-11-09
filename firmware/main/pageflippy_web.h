// ESP32Pageflippy: WLAN (Hotspot und/oder Heimnetz) und Weboberflaeche.
#pragma once

// Startet WLAN und Webserver. Vor btstack_run_loop_execute() aufrufen.
void pageflippy_web_start(void);
// Konsolenbefehle wifi_set, wifi_clear, wifi_status registrieren.
void pageflippy_web_register_console_cmds(void);
