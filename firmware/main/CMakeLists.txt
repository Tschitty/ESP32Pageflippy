# Weboberflaeche (deutsch und englisch) gzip-komprimiert einbetten: ca. 4x kleiner,
# laedt bei vollem Funk deutlich schneller. Welche Sprache ausgeliefert wird, bestimmt
# PAGEFLIPPY_LANG_EN (siehe platformio.ini).
foreach(lang de en)
    set(html_src "${CMAKE_CURRENT_LIST_DIR}/web/index_${lang}.html")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${html_src}")
    execute_process(COMMAND ${python} -c "import gzip,sys; open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(), 9, mtime=0))" "${html_src}" "${html_src}.gz")
endforeach()

idf_component_register(SRCS "main.c" "pageflippy_platform.c" "pageflippy_hog.c" "pageflippy_config.c" "pageflippy_web.c" "pageflippy_touch.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "web/index_de.html.gz" "web/index_en.html.gz"
                    REQUIRES "bluepad32" "btstack" "esp_wifi" "esp_http_server" "esp_netif" "esp_event" "esp_timer" "nvs_flash" "json" "esp_driver_gpio" "driver")
