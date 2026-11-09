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

- **Umblättern mit dem PS5-Controller.** Standardbelegung: **R1** blättert vor, **L1** zurück. Weitere Tasten, Stick oder Touchpad lassen sich am Controller oder in der Weboberfläche dazu belegen.
- **Belegen direkt am Controller.** Steuerkreuz **oben** halten und eine Taste drücken: Diese Taste blättert ab jetzt vor (rechts vibriert 2x). Steuerkreuz **unten** halten und eine Taste drücken: Diese Taste blättert zurück (links vibriert 2x).
- **Zurücksetzen der Belegung.** **R1 + L1 + Kreuz** 5 Sekunden halten (beide Seiten vibrieren 3x).
- **Weboberfläche** über den eigenen Hotspot oder dein Heim-WLAN: Tasten an- und abhaken, WLANs suchen und mit einem Tipp verbinden, Status sehen.
- **Lichtleiste:** grün = Tolino verbunden, rot = nicht verbunden. Auf Wunsch eine kurze Vibration beim Umblättern.
- **Akku-Warnung:** Fällt der Akku des DualSense unter 20 %, spielt der Controller einmal eine kurze Sieges-Fanfare als Vibration (etwa 2,7 Sekunden). Nach dem Laden warnt er beim nächsten Absinken wieder. Die Warnung kommt auch, wenn die Vibration beim Umblättern ausgeschaltet ist.
- **Einstellungen sichern.** In der Weboberfläche lassen sich Belegung, WLAN und MQTT als Datei `pageflippy-backup.json` exportieren und später wieder importieren, zum Beispiel nach einem Neu-Flashen oder auf einem zweiten ESP32. Passwörter kommen nur mit, wenn du das Häkchen setzt. Sie stehen dann lesbar in der Datei, also nicht weitergeben. Bluetooth-Kopplungen sind nicht enthalten.
- **Updates ohne Datenverlust.** Neue Versionen spielst du in der Weboberfläche ein, per Datei oder direkt von GitHub. Einstellungen und Kopplungen bleiben dabei erhalten.
- **Automatisch aus.** Ist der Tolino 2 Minuten nicht verbunden (z. B. weil er schläft), wird der Controller getrennt und schaltet sich ab. Die Zeit ist einstellbar, 0 = nie.
- **Weitere Eingabegeräte:** Bluetooth-Tastaturen und Fernbedienungen wie die Fire TV Remote, dazu viele andere Controller, die [Bluepad32](https://github.com/ricardoquesada/bluepad32) kennt (Xbox, Switch, 8BitDo …).
- **Bluetooth-Mäuse und -Tastaturen**, die sich ohne Code koppeln lassen. Maus-Standardbelegung: linke Taste, Rad runter und Seitentaste vor blättern vor. Rechte Taste, Rad hoch und Seitentaste zurück blättern zurück. Tastatur: Pfeil rechts, Bild ab und Leertaste blättern vor, Pfeil links und Bild auf zurück. Koppeln wie beim Controller: Suche starten und die Maus oder Tastatur in den Kopplungsmodus bringen.
- **Home Assistant per MQTT.** Pageflippy erscheint automatisch als Gerät mit den Knöpfen „Vor“, „Zurück“ und „Controller koppeln“ sowie Sensoren (Tolino verbunden, Controller, letzte Eingabe, Seitenzähler). So lässt sich z. B. per Automation oder Sprachassistent umblättern.
- **Touch-Tasten am ESP32.** Ein Stück Draht, Alufolie oder Kupferband an **GPIO32 (vor)** und **GPIO33 (zurück)** wird zur Berührungstaste. Das musst du in der Weboberfläche einschalten.

## Grenzen – bitte vorher lesen

- **Nur der klassische ESP32** (z. B. ESP32-WROOM-32, ESP32-D0WD). ESP32-S2/S3/C3/C6 können kein Bluetooth Classic und damit keinen DualSense.
- **Getestet nur am Tolino Vision Color.** Andere Tolinos mit Bluetooth sollten funktionieren, wenn sie Bluetooth-Tastaturen annehmen. Kobo-Geräte wohl auch, beides ist aber ungetestet.
- **Ein Funkmodul für alles.** WLAN, Controller und Tolino teilen sich eine Antenne. Die Weboberfläche ist deshalb etwas träge, besonders während der Controller-Suche. Wenn das stört: „Bluetooth bis Neustart aus“ unter Wartung.
- **Geräte mit Code-Eingabe gehen nicht.** Tastaturen, die beim Koppeln eine PIN verlangen (ältere Bluetooth-Tastaturen), versucht der ESP32 mit „0000“. Klappt das nicht, lassen sie sich nicht koppeln.
- **Neue Controller nur während der Suche.** Die Suche läuft 60 Sekunden nach dem Start oder auf Knopfdruck. Ein bereits gekoppelter DualSense verbindet sich jederzeit von selbst, eine Fire TV Remote nur während der Suche.
- **Touch-Tasten sind ein Bastel-Feature.** Ohne angeschlossenen Draht liefern die Pins zufällige Werte, deshalb ist es standardmäßig aus. Die Empfindlichkeit hängt von Kabellänge und Material ab.
- **Home Assistant nur im Heim-WLAN.** MQTT braucht eine Verbindung zum Heim-WLAN, über den Hotspot allein geht es nicht.
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

   Die Dateien mit `_update` im Namen sind nur für spätere Updates über die Weboberfläche gedacht, nicht für das erste Aufspielen.
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
   - unter **WLAN** auf „WLANs suchen“ tippen, dein Netz wählen und das Passwort eingeben. Danach verbindet sich der ESP32 mit deinem Heim-WLAN und schaltet den Hotspot ab. Die Seite findest du dann unter der IP-Adresse aus deinem Router (oder `http://esp32pageflippy`, je nach Router). Ist das Heim-WLAN 5 Minuten lang weg, geht der Hotspot automatisch an.
   - **Hotspot spart sich selbst ab:** Verbindet sich 5 Minuten lang niemand mit dem Hotspot, schaltet sich das WLAN ganz ab, bis der ESP32 neu startet. So gibt es kein verwaistes WLAN, und Bluetooth läuft störungsfrei. Das gilt auch beim allerersten Start, also innerhalb von 5 Minuten verbinden oder den ESP32 neu starten.
   - **BOOT-Taste 3 Sekunden halten** schaltet den Hotspot bis zum Neustart ein, auch wenn das WLAN schon aus war.
   - das Hotspot-Passwort ändern (empfohlen)
   - Touch-Tasten und Abschaltzeit einstellen

### 5. Home Assistant (optional)

1. In Home Assistant einen MQTT-Broker einrichten, am einfachsten das Add-on **Mosquitto broker** und die Integration **MQTT**. Einen Benutzer für Pageflippy anlegen.
2. Pageflippy mit dem Heim-WLAN verbinden (siehe oben).
3. In der Weboberfläche unter **Home Assistant (MQTT)**: einschalten, IP-Adresse des Brokers sowie Benutzer und Passwort eintragen und **Speichern und neu starten** drücken.
4. Unter *Einstellungen → Geräte & Dienste → MQTT* erscheint **ESP32Pageflippy** mit seinen Knöpfen und Sensoren.

Für eigene Skripte: Befehle `next`, `prev` oder `pair` an das Topic `pageflippy/<id>/cmd` senden. Der Status steht als JSON in `pageflippy/<id>/state`. Die `<id>` zeigt die Weboberfläche an.

Ohne MQTT geht es auch per HTTP: `POST http://<ip>/api/action?do=next` (bzw. `prev`), z. B. mit `rest_command` in Home Assistant.

### 6. Updates einspielen, ohne etwas zu verlieren

Ein Update über die Weboberfläche ersetzt nur die Firmware. Belegung, WLAN, MQTT und alle Bluetooth-Kopplungen bleiben erhalten. Dafür hat der ESP32 zwei Firmware-Bereiche: Das Update landet im unbenutzten Bereich, und erst nach bestandener Prüfung wird umgeschaltet. Startet die neue Version nicht sauber, läuft beim nächsten Start wieder die alte.

In der Weboberfläche unter **Firmware-Update** gibt es zwei Wege:

- **Von GitHub aktualisieren:** Der ESP32 lädt die neueste Version selbst aus den Releases dieses Repositorys. Dafür muss er mit dem Heim-WLAN verbunden sein. **Nach Updates suchen** zeigt vorher, ob es eine neuere Version gibt.
- **Update-Datei hochladen:** Lade `ESP32Pageflippy_DE_update.bin` (oder `EN`) herunter und wähle sie aus. Das geht auch über den Hotspot ohne Internet.

Während des Updates ist Bluetooth aus. Danach startet der ESP32 neu, das dauert etwa eine Minute.

> **Wichtig beim Umstieg:** Ältere Versionen ohne den Abschnitt „Firmware-Update“ haben nur einen Firmware-Bereich. Dort musst du **einmalig** das Komplett-Image wie in Schritt 1 aufspielen. Dabei gehen Einstellungen und Kopplungen ein letztes Mal verloren. Exportiere vorher, falls vorhanden, deine Einstellungen unter **Einstellungen sichern**.
>
> Das Komplett-Image an Adresse 0x0 löscht immer alles. Für normale Updates also immer den Weg über die Weboberfläche nehmen.

## Hilfe bei Problemen

| Problem | Lösung |
|---|---|
| Tolino verbindet sich und trennt sofort wieder | Kopplung am Tolino entfernen und neu koppeln. |
| Controller verbindet sich nicht | Suche starten (BOOT-Taste), dann PS + Create halten. Hilft das nicht: Weboberfläche → Wartung → „Alle Kopplungen löschen“ und alles neu koppeln. |
| Belegung verbastelt | R1 + L1 + Kreuz 5 Sekunden halten, oder „Standard wiederherstellen“ in der Weboberfläche. |
| Weboberfläche sehr langsam | Kurz warten, bis die Controller-Suche vorbei ist, oder „Bluetooth bis Neustart aus“. |
| Hotspot ist weg | Mit erreichbarem Heim-WLAN ist der Hotspot absichtlich aus. Nach 5 Minuten ohne Nutzung schaltet er sich ebenfalls ab. BOOT-Taste 3 Sekunden halten, ESP32 neu starten oder seriell `wifi_clear`. |
| Hotspot-Passwort vergessen | Firmware mit „Erase Flash“ neu aufspielen. Danach gilt wieder **pageflippy**, die Kopplungen sind dann weg. |
| Touch-Taste blättert von selbst | Touch-Tasten in der Weboberfläche ausschalten oder ein kürzeres Kabel nehmen. |

**Serielle Konsole** (für Neugierige): Im esptool-js-Fenster unter *Console* mit 115200 Baud verbinden. Du siehst Meldungen, und es gibt diese Befehle:

| Befehl | Wirkung |
|---|---|
| `wifi_set "Mein WLAN" geheim123` | Heim-WLAN eintragen (SSID, Passwort) und neu starten. Ohne Passwort = offenes WLAN |
| `wifi_clear` | Heim-WLAN entfernen, nur Hotspot |
| `wifi_status` | WLAN-Status und IP-Adresse anzeigen |
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

Das Komplett-Image liegt unter `.pio/build/esp32_de/firmware.factory.bin` bzw. `esp32_en`, die Update-Datei daneben als `firmware.bin`.

**Releases automatisch bauen:** Der Workflow `.github/workflows/release.yml` baut beide Sprachen und hängt alle vier Dateien an ein Release, sobald du einen Tag wie `v1.0.1` pushst (`git tag v1.0.1 && git push origin v1.0.1`). Ohne Kommandozeile geht es auch: auf GitHub unter *Releases → Draft a new release* den Tag `v1.0.1` eintragen und veröffentlichen, die Dateien kommen nach einigen Minuten dazu. Die Versionsnummer kommt aus dem Tag. Für lokale Builds steht sie in `firmware/version.txt`.

**Eigener Fork:** Trage in `firmware/platformio.ini` bei `PAGEFLIPPY_UPDATE_REPO` dein Repository ein, sonst holt der ESP32 seine Updates weiter aus dem Original. Das Repository muss öffentlich sein.
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

- **Turn pages with the PS5 controller.** Default mapping: **R1** = next page, **L1** = previous page. More buttons, the stick or the touchpad can be added on the controller or in the web interface.
- **Assign buttons on the controller.** Hold D-pad **up** and press a button: that button now turns to the next page (right side vibrates 2x). Hold D-pad **down** and press a button: that button turns back (left side vibrates 2x).
- **Reset the mapping.** Hold **R1 + L1 + Cross** for 5 seconds (both sides vibrate 3x).
- **Web interface** via the built-in hotspot or your home Wi-Fi: tick buttons, scan for Wi-Fi networks and connect with one tap, see the status.
- **Light bar:** green = Tolino connected, red = not connected. Optional short vibration when turning a page.
- **Low battery warning:** When the DualSense battery drops below 20 %, the controller plays a short victory fanfare as vibration once (about 2.7 seconds). After charging, it warns again on the next drop. The warning also plays when the page-turn vibration is turned off.
- **Back up settings.** The web interface can export mapping, Wi-Fi and MQTT to a file `pageflippy-backup.json` and import it again later, for example after a full reflash or on a second ESP32. Passwords are only included if you tick the box. They are then readable in the file, so do not share it. Bluetooth pairings are not included.
- **Updates without data loss.** Install new versions in the web interface, from a file or straight from GitHub. Settings and pairings are kept.
- **Auto power-off.** If the Tolino has been disconnected for 2 minutes (e.g. asleep), the controller is disconnected and turns off. The time is adjustable, 0 = never.
- **Other input devices:** Bluetooth keyboards and remotes such as the Fire TV Remote, plus many controllers supported by [Bluepad32](https://github.com/ricardoquesada/bluepad32) (Xbox, Switch, 8BitDo …).
- **Bluetooth mice and keyboards** that pair without a code. Default mouse mapping: left button, wheel down and side button forward = next page. Right button, wheel up and side button back = previous page. Keyboard: arrow right, page down and space = next, arrow left and page up = back. Pair them like a controller: start a search and put the mouse or keyboard into pairing mode.
- **Home Assistant via MQTT.** Pageflippy shows up automatically as a device with the buttons "Next page", "Previous page" and "Pair controller" plus sensors (Tolino connected, controller, last input, page counter). Turn pages from automations or a voice assistant.
- **Touch keys on the ESP32.** A piece of wire, aluminium foil or copper tape on **GPIO32 (next)** and **GPIO33 (back)** becomes a touch button. Enable this in the web interface.

## Limitations – please read first

- **Classic ESP32 only** (e.g. ESP32-WROOM-32, ESP32-D0WD). ESP32-S2/S3/C3/C6 have no Bluetooth Classic and cannot talk to a DualSense.
- **Only tested on the Tolino Vision Color.** Other Tolinos with Bluetooth should work if they accept Bluetooth keyboards. Kobo devices probably do too. Both are untested.
- **One radio for everything.** Wi-Fi, controller and Tolino share a single antenna, so the web interface is somewhat sluggish, especially during the controller search. If that bothers you, use "Bluetooth off until restart" under Maintenance.
- **Devices that need a code don't work.** Keyboards that ask for a PIN when pairing (older Bluetooth keyboards) are tried with "0000". If that fails, they can't be paired.
- **New controllers only during the search.** The search runs for 60 seconds after start-up or on demand. A paired DualSense reconnects at any time, a Fire TV Remote only during the search.
- **Touch keys are a tinkering feature.** Unconnected pins give random readings, so the feature is off by default. Sensitivity depends on wire length and material.
- **Home Assistant needs the home Wi-Fi.** MQTT only works while Pageflippy is connected to your home Wi-Fi, not via the hotspot alone.
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

   The files with `_update` in their name are only for later updates through the web interface, not for the first flash.
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
   - under **Wi-Fi**, tap "Search Wi-Fi networks", pick your network and enter its password. The ESP32 then connects to your home Wi-Fi and turns the hotspot off. Find the page at the IP address shown in your router (or `http://esp32pageflippy`, depending on the router). If the home Wi-Fi is gone for 5 minutes, the hotspot turns on automatically.
   - **The hotspot switches itself off:** if nobody connects to it for 5 minutes, Wi-Fi turns off completely until the ESP32 restarts. No orphaned network, and Bluetooth runs undisturbed. This also applies on the very first start, so connect within 5 minutes or restart the ESP32.
   - **Hold the BOOT button for 3 seconds** to turn the hotspot on until the next restart, even if Wi-Fi was already off.
   - change the hotspot password (recommended)
   - set up the touch keys and the power-off time

### 5. Home Assistant (optional)

1. Set up an MQTT broker in Home Assistant, the easiest is the **Mosquitto broker** add-on plus the **MQTT** integration. Create a user for Pageflippy.
2. Connect Pageflippy to your home Wi-Fi (see above).
3. In the web interface under **Home Assistant (MQTT)**: enable it, enter the broker's IP address, user and password, and press **Save and restart**.
4. **ESP32Pageflippy** appears under *Settings → Devices & services → MQTT* with its buttons and sensors.

For your own scripts: send `next`, `prev` or `pair` to the topic `pageflippy/<id>/cmd`. The status is published as JSON on `pageflippy/<id>/state`. The web interface shows the `<id>`.

Without MQTT you can also use HTTP: `POST http://<ip>/api/action?do=next` (or `prev`), e.g. with `rest_command` in Home Assistant.

### 6. Updating without losing anything

An update through the web interface only replaces the firmware. Mapping, Wi-Fi, MQTT and all Bluetooth pairings are kept. The ESP32 has two firmware areas for this: the update goes into the unused one, and the switch only happens after it passed the check. If the new version does not start properly, the old one runs again on the next start.

The web interface offers two ways under **Firmware update**:

- **Update from GitHub:** the ESP32 downloads the latest version from this repository's releases by itself. It must be connected to your home Wi-Fi. **Check for updates** shows beforehand whether a newer version exists.
- **Upload update file:** download `ESP32Pageflippy_EN_update.bin` (or `DE`) and select it. This also works through the hotspot without internet.

Bluetooth is off during the update. Afterwards the ESP32 restarts, which takes about a minute.

> **Important when upgrading:** older versions without the "Firmware update" section only have one firmware area. There you must flash the full image **once** as in step 1. Settings and pairings are lost one last time. If available, export your settings under **Back up settings** first.
>
> The full image at address 0x0 always erases everything. For normal updates always use the web interface.

## Troubleshooting

| Problem | Solution |
|---|---|
| Tolino connects and disconnects immediately | Remove the pairing on the Tolino and pair again. |
| Controller doesn't connect | Start a search (BOOT button), then hold PS + Create. If that doesn't help: web interface → Maintenance → "Delete all pairings" and pair everything again. |
| Mapping messed up | Hold R1 + L1 + Cross for 5 seconds, or "Restore defaults" in the web interface. |
| Web interface very slow | Wait until the controller search has ended, or use "Bluetooth off until restart". |
| Hotspot is gone | With a reachable home Wi-Fi the hotspot is off on purpose. It also switches off after 5 minutes without use. Hold the BOOT button for 3 seconds, restart the ESP32 or use `wifi_clear` over serial. |
| Forgot hotspot password | Re-flash with "Erase Flash". The password is **pageflippy** again, but all pairings are gone. |
| Touch key turns pages by itself | Turn touch keys off in the web interface or use a shorter wire. |

**Serial console** (for the curious): In esptool-js, connect under *Console* at 115200 baud. You'll see log messages, and these commands are available:

| Command | Effect |
|---|---|
| `wifi_set "My WiFi" secret123` | set home Wi-Fi (SSID, password) and restart. No password = open network |
| `wifi_clear` | remove home Wi-Fi, hotspot only |
| `wifi_status` | show Wi-Fi status and IP address |
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

The full image ends up in `.pio/build/esp32_en/firmware.factory.bin` (or `esp32_de`), the update file next to it as `firmware.bin`.

**Automatic releases:** the workflow `.github/workflows/release.yml` builds both languages and attaches all four files to a release as soon as you push a tag such as `v1.0.1` (`git tag v1.0.1 && git push origin v1.0.1`). It also works without the command line: on GitHub under *Releases → Draft a new release*, enter the tag `v1.0.1` and publish; the files are added a few minutes later. The version number comes from the tag. For local builds it is in `firmware/version.txt`.

**Your own fork:** set `PAGEFLIPPY_UPDATE_REPO` in `firmware/platformio.ini` to your repository, otherwise the ESP32 keeps pulling updates from the original. The repository must be public.
Tested with Bluepad32 5.0.0-beta0 (commit 80d6314), ESP-IDF 5.5.5 (pioarduino), ESP32-D0WD-V3.
