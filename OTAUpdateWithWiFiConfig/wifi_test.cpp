/*
MIT License

Copyright (c) 2025 JoergBruenner

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"
#include "lwip/dns.h"
#include "lwip/dhcp.h"
#include "dhcpserver.h"
#include <stdlib.h>
#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/psm.h"
#include "lwip/apps/http_client.h"

#ifdef __cplusplus
    extern "C" {
#endif
#include "pico-w-wifi-configure.h"
#ifdef __cplusplus
}
#endif

// Reset Pin Configuration
#define RESET_PIN 4

// OTA Update Configuration
#define CURRENT_VERSION "0.4"
#define UPDATE_SERVER "raspberrypi4" 
#define UPDATE_PORT 80
#define UPDATE_PATH "/firmware.bin"
#define VERSION_PATH "/version.txt"

// Internal Flash Storage Configuration (Last sector of 2MB flash)
#define FLASH_CONFIG_OFFSET (2048 * 1024 - FLASH_SECTOR_SIZE)
struct FlashConfig {
    char ssid[33];
    char password[65];
    uint32_t magic;
};
#define CONFIG_MAGIC 0x57494649 // "WIFI" in hex

// Flash offset where the new image will be stored (e.g., at 1MB mark)
#define FLASH_TARGET_OFFSET (1024 * 1024)

static uint32_t current_write_offset = 0;
static bool is_checking_version = false;
static char remote_version_buffer[16];
static uint16_t version_buffer_idx = 0;
static uint8_t page_buffer[FLASH_PAGE_SIZE];
static uint16_t buffer_index = 0;
static volatile bool update_complete = false;
static volatile bool update_error = false;
static volatile bool ota_done = false;

// other static vaiables
static char current_ssid[33];
static char current_pass[65];
static ip_addr_t ip_addr;
static volatile bool dns_resolved = false;

// Must be static/global because the HTTP client stores a pointer to this
static httpc_connection_t settings;

int wait_time = 1000;

// Flash offset where the new image will be stored (e.g., at 1MB mark)
#define FLASH_TARGET_OFFSET (1024 * 1024)



// This function must run from RAM because it overwrites the flash where the code normally executes.
void __not_in_flash_func(install_update_and_reboot)() {
    // 0. Kill the watchdog immediately.
    watchdog_hw->ctrl = 0;
    
    uint32_t ints = save_and_disable_interrupts();

    // 1. Lookup Bootrom functions. These are in ROM and always safe to call.
    auto rom_connect_internal_flash = (void (*)(void))rom_func_lookup_inline(ROM_TABLE_CODE('I', 'F'));
    auto rom_flash_exit_xip = (void (*)(void))rom_func_lookup_inline(ROM_TABLE_CODE('E', 'X'));
    auto rom_flash_range_erase = (void (*)(uint32_t, size_t, uint32_t, uint8_t))rom_func_lookup_inline(ROM_TABLE_CODE('R', 'E'));
    auto rom_flash_range_program = (void (*)(uint32_t, const uint8_t *, size_t))rom_func_lookup_inline(ROM_TABLE_CODE('R', 'P'));
    auto rom_flash_flush_cache = (void (*)(void))rom_func_lookup_inline(ROM_TABLE_CODE('F', 'C'));
    auto rom_flash_enter_cmd_xip = (void (*)(void))rom_func_lookup_inline(ROM_TABLE_CODE('C', 'X'));

    // 2. Setup Flash for basic SPI access
    rom_connect_internal_flash();
    rom_flash_exit_xip();

    uint32_t size = current_write_offset;
    uint32_t offset = 0;
    uint8_t page_buf[FLASH_PAGE_SIZE];

    // 3. Sector-by-sector copy
    while (offset < size) {
        // Erase 4KB at the start of flash
        rom_flash_exit_xip();
        rom_flash_range_erase(offset, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE, 0x20);

        for (uint32_t page = 0; page < FLASH_SECTOR_SIZE; page += FLASH_PAGE_SIZE) {
            uint32_t current_addr = offset + page;

            // Read from 1MB mark. We must use Enter_XIP to use the memory-mapped address.
            // This Bootrom function resets the SSI to a standard SPI read mode.
            rom_flash_flush_cache();
            rom_flash_enter_cmd_xip();
            
            const uint8_t *src = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET + current_addr);
            for (int i = 0; i < FLASH_PAGE_SIZE; i++) {
                page_buf[i] = src[i];
            }

            // Program to address 0+
            rom_flash_exit_xip();
            rom_flash_range_program(current_addr, page_buf, FLASH_PAGE_SIZE);
        }
        offset += FLASH_SECTOR_SIZE;
    }

    // 4. Force Reboot
    rom_flash_flush_cache();
    rom_flash_enter_cmd_xip();
    
    psm_hw->wdsel = 0x0001fff7;
    watchdog_hw->ctrl = WATCHDOG_CTRL_TRIGGER_BITS;
    while (1);
}

// Helper to write a full page to flash
void commit_page_to_flash() {
    if (buffer_index == 0 || update_error) return;

    // Safety check: Ensure OTA staging doesn't overwrite the config sector at the end of flash
    if (current_write_offset + FLASH_PAGE_SIZE > (1024 * 1024 - FLASH_SECTOR_SIZE)) {
        update_error = true;
        return;
    }

    // Erase sector if we are at the start of a 4KB block
    if ((current_write_offset % FLASH_SECTOR_SIZE) == 0) {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(FLASH_TARGET_OFFSET + current_write_offset, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
    }

    // Pad remaining buffer with 0xFF if not a full page (only happens at EOF)
    if (buffer_index > 0 && buffer_index < FLASH_PAGE_SIZE) {
        memset(page_buffer + buffer_index, 0xFF, FLASH_PAGE_SIZE - buffer_index);
    }

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(FLASH_TARGET_OFFSET + current_write_offset, page_buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    current_write_offset += FLASH_PAGE_SIZE;
    buffer_index = 0;
    printf(".");
}

// Callback function for HTTP data reception
static err_t http_recv_callback(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err) {
    if (update_error) {
        if (p) pbuf_free(p);
        return ERR_ABRT;
    }

    if (p == NULL) {
        if (!is_checking_version) {
            if (buffer_index > 0) commit_page_to_flash();
            printf("\nOTA: Download finished. Total flash written: %u bytes\n", current_write_offset);
        }
        return ERR_OK;
    }

    if (is_checking_version) {
        // Handle version string download
        for (uint16_t i = 0; i < p->tot_len && version_buffer_idx < sizeof(remote_version_buffer) - 1; i++) {
            remote_version_buffer[version_buffer_idx++] = pbuf_get_at(p, i);
        }
        remote_version_buffer[version_buffer_idx] = '\0';
    } else {
        // Handle firmware binary download
        for (uint16_t i = 0; i < p->tot_len; i++) {
            page_buffer[buffer_index++] = pbuf_get_at(p, i);
            if (buffer_index == FLASH_PAGE_SIZE) {
                commit_page_to_flash();
            }
        }
    }

    altcp_recved(conn, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

// Forward declaration to trigger actual firmware download
void start_firmware_download();

void start_ota_update() {
    // Reset state for new update attempt
    current_write_offset = 0;
    buffer_index = 0;
    version_buffer_idx = 0;
    is_checking_version = true;
    update_complete = false;
    update_error = false;
    memset(remote_version_buffer, 0, sizeof(remote_version_buffer));

    printf("OTA: Checking version at %s%s...\n", UPDATE_SERVER, VERSION_PATH);
    memset(&settings, 0, sizeof(settings));
    
    settings.result_fn = [](void *arg, httpc_result_t httpc_result, uint32_t rx_content_len, uint32_t srv_res, err_t err) {
        if (srv_res != 200) {
            printf("\nOTA: Error - Server returned HTTP %u\n", srv_res);
            update_error = true;
            ota_done = true;
            return;
        }

        if (is_checking_version) {
            char* endptr;
            float remote_v = strtof(remote_version_buffer, &endptr);
            float local_v = strtof(CURRENT_VERSION, NULL);
            
            printf("OTA: Local version: %s, Remote version: %s\n", CURRENT_VERSION, remote_version_buffer);
            
            if (remote_v > local_v) {
                printf("OTA: New version available. Starting download...\n");
                start_firmware_download();
            } else {
                printf("OTA: Firmware is up to date.\n");
                ota_done = true;
            }
            return;
        }
        printf("\nOTA: HTTP Request finished. Result: %d\n", httpc_result);
        
        if (httpc_result == HTTPC_RESULT_OK && !update_error) {
            if (buffer_index > 0) commit_page_to_flash();
            update_complete = true;
        } else {
            update_error = true;
            ota_done = true;
        }
    };

    err_t err = httpc_get_file_dns(UPDATE_SERVER, UPDATE_PORT, VERSION_PATH, &settings, http_recv_callback, NULL, NULL);
    if (err != ERR_OK) {
        printf("OTA: Failed to initiate version check.\n");
    }
}

void start_firmware_download() {
    is_checking_version = false;
    current_write_offset = 0;
    buffer_index = 0;
    err_t err = httpc_get_file_dns(UPDATE_SERVER, UPDATE_PORT, UPDATE_PATH, &settings, http_recv_callback, NULL, NULL);
    if (err != ERR_OK) {
        printf("OTA: Failed to initiate firmware download.\n");
        update_error = true;
    }
}

// -----------------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------------

static void dns_result_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    if (ipaddr != NULL) {
        ip_addr = *ipaddr;
        dns_resolved = true;
    } else {
        printf("DNS resolution failed for %s\n", name);
        dns_resolved = true;  // Signal completion to avoid an infinite wait
    }
}

// Helper to save credentials to internal flash safely
void save_wifi_to_flash(const char* ssid, const char* pass) {
    FlashConfig config = {0};
    strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
    strncpy(config.password, pass, sizeof(config.password) - 1);
    config.magic = CONFIG_MAGIC;

    // Data must be written in page-sized chunks (256 bytes)
    uint8_t write_buffer[FLASH_PAGE_SIZE];
    memset(write_buffer, 0, FLASH_PAGE_SIZE);
    memcpy(write_buffer, &config, sizeof(config));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, write_buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main() {

    stdio_init_all();
    printf("Starting WiFi OTA update example (C++)...\n");

    gpio_init(RESET_PIN);
    gpio_set_dir(RESET_PIN, GPIO_IN);
    gpio_pull_down(RESET_PIN);

    // 3. Initialize WiFi 
    while (cyw43_arch_init()) {
        printf("WiFi init failed\n");
        sleep_ms(100);
    }

    // Initialize flash configuration pointer
    const FlashConfig* flash_conf = (const FlashConfig*)(XIP_BASE + FLASH_CONFIG_OFFSET);
    bool has_config = (flash_conf->magic == CONFIG_MAGIC);

    dhcp_server_t dhcp_server = {0};

    if ((gpio_get(RESET_PIN) == 1) || !has_config) { 
        printf("Getting WiFi: Reset pin is high or no config found: starting WiFi config...\n");
        const char *ap_name = "PicoW-Config";
        cyw43_arch_enable_ap_mode(ap_name, NULL, CYW43_AUTH_OPEN);

        ip_addr_t gw, mask;
        ip4_addr_set_u32(ip_2_ip4(&gw), PP_HTONL(LWIP_MAKEU32(192,168,4,1)));
        ip4_addr_set_u32(ip_2_ip4(&mask), PP_HTONL(LWIP_MAKEU32(255,255,255,0)));

        // Start the dhcp server (pico-examples API)
        dhcp_server_init(&dhcp_server, &gw, &mask);

        wifi_config_t wifi_config = {0};
        if (run_wifi_config_server(&wifi_config) != 0) {
            printf("Getting WiFi: Failed to run wifi config server\n");
            dhcp_server_deinit(&dhcp_server);
            return -1;
        }
        else {
            printf("Getting WiFi: WiFi configuration received: SSID='%s'\n", wifi_config.ssid);
            dhcp_server_deinit(&dhcp_server);
            cyw43_arch_disable_ap_mode();

            // Save to internal flash instead of EEPROM
            save_wifi_to_flash(wifi_config.ssid, wifi_config.password);
            printf("Getting WiFi: WiFi credentials saved to internal flash\n");
            
            strncpy(current_ssid, wifi_config.ssid, 32);
            strncpy(current_pass, wifi_config.password, 64);
        }
    } else {
        strncpy(current_ssid, flash_conf->ssid, 32);
        strncpy(current_pass, flash_conf->password, 64);
    }

    cyw43_arch_enable_sta_mode();
    int wifi_connect_retries = 0;
    const int wifi_connect_max_retries = 5;

    printf("Getting WiFi: mode switched to station mode\n");
    printf("Getting WiFi: Connecting to WiFi: %s\n", current_ssid);
    while (wifi_connect_retries < wifi_connect_max_retries &&
        cyw43_arch_wifi_connect_timeout_ms(current_ssid, current_pass, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Getting WiFi: WiFi connection failed\n");
        sleep_ms(1000);
        wifi_connect_retries++;
    }

        if (wifi_connect_retries < wifi_connect_max_retries) {
            printf("Getting WiFi: WiFi connected.\n");

            // DNS resolution
            printf("Using WiFi: Resolving DNS... %s\n", UPDATE_SERVER);
            dns_resolved = false;
            err_t dns_err = dns_gethostbyname(UPDATE_SERVER, &ip_addr, dns_result_callback, NULL);
            if (dns_err == ERR_OK) {
                printf("Using WiFi: DNS resolution successful. IP address: %s\n", ip4addr_ntoa(&ip_addr));
            } else if (dns_err == ERR_INPROGRESS) {
                int dns_wait_ms = 0;
                const int dns_wait_timeout_ms = 10000; // 10 seconds
                while (!dns_resolved && dns_wait_ms < dns_wait_timeout_ms) {
                    sleep_ms(100);
                    dns_wait_ms += 100;
                }
                if (dns_resolved && ip_addr.addr != 0) {
                    printf("Using WiFi: DNS resolution successful. IP address: %s\n", ip4addr_ntoa(&ip_addr));
                } else {
                    printf("Using WiFi: DNS resolution failed or timed out.\n");
                    return -1;
                }
            } else {
                printf("Using WiFi: DNS resolution failed to start.\n");
                return -1;
            }
        } else {
            printf("Using WiFi: WiFi connection failed after %d retries.\n", wifi_connect_max_retries);
            return -1;
        }

    // Trigger OTA Update
    start_ota_update();

    while (!ota_done) {
        if (update_complete) {
            printf("OTA: Update successful! Finalizing and rebooting in 4 seconds...\n");
            sleep_ms(4000);
            printf("OTA: Erasing old firmware and installing new image (this takes ~15s)...\n");
            install_update_and_reboot();
        }
        
        if (update_error) {
            printf("OTA: Update failed. Check server and Wi-Fi.\n");
            break; // Exit the loop to run productive code despite OTA failure
        }

        sleep_ms(1000);
    }
    // OTA update code ends here. 

    // insert some code to show working sample ...
    printf("OTA: productive code starts here ...\n");   // or somwhere else, it has to be tested ...
    while (true) {
        sleep_ms(1000);
        printf("OTA: running new productive code ...\n");
    }

    return 0;
}
