// ESP32Pageflippy: Sprache der Firmware. Deutsch ist Standard, Englisch mit -DPAGEFLIPPY_LANG_EN.
// Language of the firmware. German by default, English with -DPAGEFLIPPY_LANG_EN.
#pragma once

#ifdef PAGEFLIPPY_LANG_EN
#define T(de, en) en
#else
#define T(de, en) de
#endif
