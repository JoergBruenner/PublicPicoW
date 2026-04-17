/*
 * Copyright (c) 2023 Gerhard G.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PICO_W_WIFI_CONFIGURE_H
#define PICO_W_WIFI_CONFIGURE_H

#include "pico/cyw43_arch.h"

#define WIFI_SSID_MAX_CHARS 32
#define WIFI_PASSWORD_MAX_CHARS 64

typedef struct {
    char ssid[WIFI_SSID_MAX_CHARS + 1];
    char password[WIFI_PASSWORD_MAX_CHARS + 1];
    // cyw43_country_t country; // This field is not used and causes a compile error.
} wifi_config_t;

// Function to start the configuration process
// Returns 0 on success, -1 on failure
int run_wifi_config_server(wifi_config_t* config);

#endif // PICO_W_WIFI_CONFIGURE_H