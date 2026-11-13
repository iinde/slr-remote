// Role applications + boot menu + serial console.
#pragma once
#include "store.h"

// Blocking setup menu (PRG held at boot, RST tapped twice, or asked for by the app/console).
// start: 0 menu only, 2 start camera pairing, 3 start board pairing (MenuNext values).
void menu_run(Settings& s, uint8_t start = 0);
void app_handheld_setup(Settings& s);
void app_handheld_loop();
void app_camera_setup(Settings& s);
void app_camera_loop();
// Serial console (USB CDC, 115200): help, stat, dump, clear, notify, forget-cam,
// forget-pair, role <h|c>, mode <direct|relay>, sf <auto|7..12>, focus <ms>,
// gain <dBi>, geotag <on|off>, reset, reboot
// in_menu: called from the setup menu, where no role app is running. Settings changed here
// are saved; the running role/mode never changes before a reboot.
void serial_poll(Settings& s, bool in_menu = false);
