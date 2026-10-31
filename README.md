# ESP32Pageflippy

**🇩🇪 Deutsch** · [🇬🇧 English](#english)

Mit einem PS5-Controller (DualSense) **im Tolino umblättern**. Ein ESP32 meldet sich beim Tolino als
Bluetooth-Tastatur und schickt bei jedem Tastendruck „Pfeil rechts“ (vor) oder „Pfeil links“ (zurück).
Belegung, WLAN und alles andere stellst du bequem im Browser ein.

```
DualSense  ──Bluetooth Classic──▶  ESP32  ──Bluetooth LE (Tastatur)──▶  Tolino
                                     │
                                     └── WLAN-Hotspot mit Weboberfläche (Handy / PC)
```

## Was es kann

- **Umblättern mit dem PS5-Controller.** Standardbelegung: vor = R1, R2, Kreuz, Steuerkreuz rechts, linker Stick rechts, Touchpad nach links wischen oder drücken. Zurück = L1, L2, Quadrat, Steuerkreuz links, linker Stick links, Touchpad nach rechts wischen oder rechtes Viertel drücken.
- **Belegen direkt am Controller.** Steuerkreuz **oben** halten und eine Taste drücken: Diese Taste blättert ab jetzt vor (rechts vibriert 2x). Steuerkreuz **unten** halten und eine Taste drücken: Diese Taste blättert zurück (links vibriert 2x).
- **Zurücksetzen der Belegung.** **R1 + L1 + Kreuz** 5 Sekunden halten (beide Seiten vibrieren 3x).
- **Weboberfläche** über den eigenen Hotspot oder dein Heim-WLAN: Tasten an- und abhaken, WLANs suchen und mit einem Tipp verbinden, Status sehen.
- **Lichtleiste:** grün = Tolino verbunden, rot = nicht verbunden. Auf Wunsch eine kurze Vibration beim Umblättern.
- **Automatisch aus.** Ist der Tolino 2 Minuten nicht verbunden (z. B. weil er schläft), wird der Controller getrennt und schaltet sich ab. Die Zeit ist einstellbar, 0 = nie.
- **Weitere Eingabegeräte:** Bluetooth-Tastaturen und Fernbedienungen wie die Fire TV Remote, dazu viele andere Controller, die [Bluepad32](https://github.com/ricardoquesada/bluepad32) kennt (Xbox, Switch, 8BitDo …).
- **Touch-Tasten am ESP32.** Ein Stück Draht, Alufolie oder Kupferband an **GPIO32 (vor)** und **GPIO33 (zurück)** wird zur Berührungstaste. Das musst du in der Weboberfläche einschalten.

## Grenzen – bitte vorher lesen

- **Nur der klassische ESP32** (z. B. ESP32-WROOM-32, ESP32-D0WD). ESP32-S2/S3/C3/C6 können kein Bluetooth Classic und damit keinen DualSense.
- **Getestet nur am Tolino Vision Color.** Andere Tolinos mit Bluetooth sollten funktionieren, wenn sie Bluetooth-Tastaturen annehmen. Kobo-Geräte wohl auch, beides ist aber ungetestet.
- **Ein Funkmodul für alles.** WLAN, Controller und Tolino teilen sich eine Antenne. Die Weboberfläche ist deshalb etwas träge, besonders während der Controller-Suche. Wenn das stört: „Bluetooth bis Neustart aus“ unter Wartung.
- **Neue Controller nur während der Suche.** Die Suche läuft 60 Sekunden nach dem Start oder auf Knopfdruck. Ein bereits gekoppelter DualSense verbindet sich jederzeit von selbst, eine Fire TV Remote nur während der Suche.
- **Touch-Tasten sind ein Bastel-Feature.** Ohne angeschlossenen Draht liefern die Pins zufällige Werte, deshalb ist es standardmäßig aus. Die Empfindlichkeit hängt von Kabellänge und Material ab.
- **Lizenz:** Das Projekt nutzt Bluepad32 (Apache 2.0) und BTstack von BlueKitchen. BTstack ist **nur für nicht-kommerzielle bzw. Open-Source-Nutzung** frei. Wer das Gerät verkaufen will, braucht eine Lizenz von BlueKitchen.

## Was du brauchst

| Teil | Hinweis |
|---|---|
| ESP32-Entwicklerboard (klassischer ESP32) | z. B. „ESP32 DevKit V1“ / ESP32-WROOM-32, ca. 5–10 € |
| USB-Datenkabel | kein reines Ladekabel |
| PS5-Controller (DualSense) | oder ein anderer Bluetooth-Controller |
| Tolino mit Bluetooth | getestet: Vision Color |
| PC mit Chrome oder Edge | zum Flashen, nur einmal nötig |

## Schritt für Schritt

### 1. Firmware aufspielen (einmalig, ca. 5 Minuten)

1. Lade die passende Datei aus [`bin/`](bin/) oder von der Releases-Seite herunter:
   - `ESP32Pageflippy_DE.bin` → Weboberfläche und Meldungen auf **Deutsch**
   - `ESP32Pageflippy_EN.bin` → **Englisch**
2. Steck den ESP32 per USB an den PC. Erkennt Windows ihn nicht, fehlt der USB-Treiber. Auf dem Chip neben dem USB-Anschluss steht meist **CP2102** ([Treiber](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers)) oder **CH340** ([Treiber](https://www.wch-ic.com/downloads/CH341SER_EXE.html)).
3. Öffne in **Chrome oder Edge** die Seite **https://espressif.github.io/esptool-js/**.
4. Baudrate 921600 lassen und auf **Connect** klicken. Wähle den Port (z. B. „CP210x“ oder „USB-SERIAL CH340“).
   Klappt es nicht, halte beim Klick auf Connect die **BOOT**-Taste am ESP32 gedrückt.
5. Beim ersten Mal **Erase Flash** klicken und warten, bis es fertig ist.
6. Bei *Flash Address* **0x0** eintragen, die `.bin`-Datei auswählen und **Program** klicken.
7. Danach **Disconnect** wählen und die **EN/RST**-Taste am ESP32 drücken. Fertig.

### 2. Tolino koppeln

1. Am Tolino: *Einstellungen → Bluetooth* einschalten und nach Geräten suchen.
2. **ESP32Pageflippy** antippen und koppeln.
3. War schon einmal ein „ESP32Pageflippy“ gekoppelt (z. B. mit älterer Firmware), entferne es vorher am Tolino.

### 3. Controller koppeln

1. Die ersten 60 Sekunden nach dem Einschalten sucht der ESP32 nach Controllern.
   Später startest du die Suche über die **BOOT**-Taste oder in der Weboberfläche mit „Neuen Controller koppeln“.
2. Am DualSense **PS + Create** (kleine Taste links oben) gedrückt halten, bis das Licht schnell blinkt.
3. Kurz vibrieren, dann leuchtet die Lichtleiste **grün** (Tolino verbunden) oder **rot** (Tolino noch nicht verbunden).
4. Ab jetzt reicht ein Druck auf die **PS-Taste**, und der Controller verbindet sich von selbst.

### 4. Weboberfläche

1. Mit dem Handy ins WLAN **ESP32Pageflippy** gehen, Passwort **pageflippy**.
2. Im Browser **http://192.168.4.1** öffnen.
3. Dort kannst du:
   - Tasten für vor/zurück an- und abhaken und speichern
   - unter „Letzte Eingabe“ sehen, welche Taste gerade gedrückt wurde
   - unter **WLAN** auf „WLANs suchen“ tippen, dein Netz wählen und das Passwort eingeben. Danach ist die Seite auch im Heim-WLAN erreichbar.
   - das Hotspot-Passwort ändern (empfohlen)
   - Touch-Tasten und Abschaltzeit einstellen

## Hilfe bei Problemen

| Problem | Lösung |
|---|---|
| Tolino verbindet sich und trennt sofort wieder | Kopplung am Tolino entfernen und neu koppeln. |
| Controller verbindet sich nicht | Suche starten (BOOT-Taste), dann PS + Create halten. Hilft das nicht: Weboberfläche → Wartung → „Alle Kopplungen löschen“ und alles neu koppeln. |
| Belegung verbastelt | R1 + L1 + Kreuz 5 Sekunden halten, oder „Standard wiederherstellen“ in der Weboberfläche. |
| Weboberfläche sehr langsam | Kurz warten, bis die Controller-Suche vorbei ist, oder „Bluetooth bis Neustart aus“. |
| Hotspot-Passwort vergessen | Firmware mit „Erase Flash“ neu aufspielen. Danach gilt wieder **pageflippy**, die Kopplungen sind dann weg. |
| Touch-Taste blättert von selbst | Touch-Tasten in der Weboberfläche ausschalten oder ein kürzeres Kabel nehmen. |

**Serielle Konsole** (für Neugierige): Im esptool-js-Fenster unter *Console* mit 115200 Baud verbinden. Du siehst Meldungen, und es gibt diese Befehle:

| Befehl | Wirkung |
|---|---|
| `bt_off` | Bluetooth bis zum Neustart aus |
| `reset_map` | Tastenbelegung auf Standard |
| `help` | alle Befehle |

## Selbst bauen (für Fortgeschrittene)

Du brauchst [PlatformIO](https://platformio.org/) (z. B. als VS-Code-Erweiterung) und Git.

```sh
git clone <dieses-repo> && cd ESP32Pageflippy/firmware
git clone https://github.com/ricardoquesada/bluepad32.git external/bluepad32
cd external/bluepad32 && git checkout 80d6314 && git submodule update --init --recursive
cd external/btstack && git apply ../patches/*.patch && cd ../..
git apply ../../patches/*.patch && cd ../..
pio run                    # baut beide Sprachen
pio run -e esp32_de        # nur Deutsch
pio run -e esp32_en        # nur Englisch
```

Das fertige Image liegt unter `.pio/build/esp32_de/firmware.factory.bin` bzw. `esp32_en`.
Getestet mit Bluepad32 5.0.0-beta0 (Commit 80d6314), ESP-IDF 5.5.5 (pioarduino), ESP32-D0WD-V3.

---

<a id="english"></a>
# ESP32Pageflippy (English)

[🇩🇪 Deutsch](#esp32pageflippy) · **🇬🇧 English**

**Turn pages on a Tolino e-reader with a PS5 controller (DualSense).** An ESP32 pairs with the Tolino as a
Bluetooth keyboard and sends "arrow right" (next page) or "arrow left" (previous page) whenever you press a button.
Button mapping, Wi-Fi and everything else are set up in your browser.

```
DualSense  ──Bluetooth Classic──▶  ESP32  ──Bluetooth LE (keyboard)──▶  Tolino
                                     │
                                     └── Wi-Fi hotspot with web interface (phone / PC)
```

## Features

- **Turn pages with the PS5 controller.** Default mapping: next = R1, R2, Cross, D-pad right, left stick right, touchpad swipe left or press. Back = L1, L2, Square, D-pad left, left stick left, touchpad swipe right or press the right quarter.
- **Assign buttons on the controller.** Hold D-pad **up** and press a button: that button now turns to the next page (right side vibrates 2x). Hold D-pad **down** and press a button: that button turns back (left side vibrates 2x).
- **Reset the mapping.** Hold **R1 + L1 + Cross** for 5 seconds (both sides vibrate 3x).
- **Web interface** via the built-in hotspot or your home Wi-Fi: tick buttons, scan for Wi-Fi networks and connect with one tap, see the status.
- **Light bar:** green = Tolino connected, red = not connected. Optional short vibration when turning a page.
- **Auto power-off.** If the Tolino has been disconnected for 2 minutes (e.g. asleep), the controller is disconnected and turns off. The time is adjustable, 0 = never.
- **Other input devices:** Bluetooth keyboards and remotes such as the Fire TV Remote, plus many controllers supported by [Bluepad32](https://github.com/ricardoquesada/bluepad32) (Xbox, Switch, 8BitDo …).
- **Touch keys on the ESP32.** A piece of wire, aluminium foil or copper tape on **GPIO32 (next)** and **GPIO33 (back)** becomes a touch button. Enable this in the web interface.

## Limitations – please read first

- **Classic ESP32 only** (e.g. ESP32-WROOM-32, ESP32-D0WD). ESP32-S2/S3/C3/C6 have no Bluetooth Classic and cannot talk to a DualSense.
- **Only tested on the Tolino Vision Color.** Other Tolinos with Bluetooth should work if they accept Bluetooth keyboards. Kobo devices probably do too. Both are untested.
- **One radio for everything.** Wi-Fi, controller and Tolino share a single antenna, so the web interface is somewhat sluggish, especially during the controller search. If that bothers you, use "Bluetooth off until restart" under Maintenance.
- **New controllers only during the search.** The search runs for 60 seconds after start-up or on demand. A paired DualSense reconnects at any time, a Fire TV Remote only during the search.
- **Touch keys are a tinkering feature.** Unconnected pins give random readings, so the feature is off by default. Sensitivity depends on wire length and material.
- **License:** This project uses Bluepad32 (Apache 2.0) and BTstack by BlueKitchen. BTstack is **free for non-commercial / open-source use only**. Selling devices requires a license from BlueKitchen.

## What you need

| Part | Note |
|---|---|
| ESP32 dev board (classic ESP32) | e.g. "ESP32 DevKit V1" / ESP32-WROOM-32, about €5–10 |
| USB data cable | not a charge-only cable |
| PS5 controller (DualSense) | or another Bluetooth controller |
| Tolino with Bluetooth | tested: Vision Color |
| PC with Chrome or Edge | for flashing, needed only once |

## Step by step

### 1. Flash the firmware (once, about 5 minutes)

1. Download the right file from [`bin/`](bin/) or the Releases page:
   - `ESP32Pageflippy_EN.bin` → web interface and messages in **English**
   - `ESP32Pageflippy_DE.bin` → **German**
2. Connect the ESP32 to your PC via USB. If Windows doesn't recognise it, install the USB driver. The chip next to the USB port usually says **CP2102** ([driver](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers)) or **CH340** ([driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html)).
3. Open **https://espressif.github.io/esptool-js/** in **Chrome or Edge**.
4. Keep baud rate 921600 and click **Connect**. Pick the port (e.g. "CP210x" or "USB-SERIAL CH340").
   If it fails, hold the **BOOT** button on the ESP32 while clicking Connect.
5. The first time, click **Erase Flash** and wait until it finishes.
6. Enter **0x0** as *Flash Address*, choose the `.bin` file and click **Program**.
7. Click **Disconnect** and press the **EN/RST** button on the ESP32. Done.

### 2. Pair the Tolino

1. On the Tolino: *Settings → Bluetooth*, switch it on and search for devices.
2. Tap **ESP32Pageflippy** and pair.
3. If an "ESP32Pageflippy" was paired before (e.g. with older firmware), remove it on the Tolino first.

### 3. Pair the controller

1. For the first 60 seconds after power-on the ESP32 searches for controllers.
   Later, start a search with the **BOOT** button or "Pair new controller" in the web interface.
2. On the DualSense hold **PS + Create** (small button top left) until the light blinks quickly.
3. It vibrates briefly, then the light bar turns **green** (Tolino connected) or **red** (Tolino not connected yet).
4. From now on a press of the **PS button** is enough, and the controller reconnects by itself.

### 4. Web interface

1. Connect your phone to the Wi-Fi **ESP32Pageflippy**, password **pageflippy**.
2. Open **http://192.168.4.1** in the browser.
3. There you can:
   - tick the buttons for next/back and save
   - see under "Last input" which button you just pressed
   - under **Wi-Fi**, tap "Search Wi-Fi networks", pick your network and enter its password. The page is then also reachable in your home Wi-Fi.
   - change the hotspot password (recommended)
   - set up the touch keys and the power-off time

## Troubleshooting

| Problem | Solution |
|---|---|
| Tolino connects and disconnects immediately | Remove the pairing on the Tolino and pair again. |
| Controller doesn't connect | Start a search (BOOT button), then hold PS + Create. If that doesn't help: web interface → Maintenance → "Delete all pairings" and pair everything again. |
| Mapping messed up | Hold R1 + L1 + Cross for 5 seconds, or "Restore defaults" in the web interface. |
| Web interface very slow | Wait until the controller search has ended, or use "Bluetooth off until restart". |
| Forgot hotspot password | Re-flash with "Erase Flash". The password is **pageflippy** again, but all pairings are gone. |
| Touch key turns pages by itself | Turn touch keys off in the web interface or use a shorter wire. |

**Serial console** (for the curious): In esptool-js, connect under *Console* at 115200 baud. You'll see log messages, and these commands are available:

| Command | Effect |
|---|---|
| `bt_off` | Bluetooth off until restart |
| `reset_map` | reset button mapping to defaults |
| `help` | list all commands |

## Building from source (advanced)

You need [PlatformIO](https://platformio.org/) (e.g. as a VS Code extension) and Git.

```sh
git clone <this-repo> && cd ESP32Pageflippy/firmware
git clone https://github.com/ricardoquesada/bluepad32.git external/bluepad32
cd external/bluepad32 && git checkout 80d6314 && git submodule update --init --recursive
cd external/btstack && git apply ../patches/*.patch && cd ../..
git apply ../../patches/*.patch && cd ../..
pio run                    # builds both languages
pio run -e esp32_en        # English only
pio run -e esp32_de        # German only
```

The image ends up in `.pio/build/esp32_en/firmware.factory.bin` (or `esp32_de`).
Tested with Bluepad32 5.0.0-beta0 (commit 80d6314), ESP-IDF 5.5.5 (pioarduino), ESP32-D0WD-V3.
