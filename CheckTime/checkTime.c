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
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"
#include "hardware/rtc.h"
#include <time.h>
#include "lwip/apps/sntp.h"

#define WIFI_SSID "WiFi SSID"
#define WIFI_PASSWORD "WiFi Password"
#define SNTP_SERVER "pool.ntp.org"

// Custom function to calculate epoch timestamp
time_t custom_mktime(datetime_t *rtc_time) {
    const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int year = rtc_time->year;
    int month = rtc_time->month;
    int day = rtc_time->day;
    int hour = rtc_time->hour;
    int minute = rtc_time->min;
    int second = rtc_time->sec;

    // Calculate days since epoch (1970-01-01)
    int days = (year - 1970) * 365 + (year - 1969) / 4; // Add leap years
    for (int i = 0; i < month - 1; i++) {
        days += days_in_month[i];
    }
    if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        days++; // Add leap day for February in leap years
    }
    days += day - 1;

    // Calculate total seconds
    time_t total_seconds = (time_t)days * 86400 + hour * 3600 + minute * 60 + second;
    return total_seconds;
}

void SNTPSetRTC(u32_t t, u32_t us)
{
    printf("updating RTC\n");   
    time_t secs = t - 2208988800;
    struct tm *datetime = gmtime(&secs);
    datetime_t dt;
    dt.year = datetime->tm_year + 1900;
    dt.month = datetime->tm_mon + 1;
    dt.day = datetime->tm_mday;
    dt.dotw = datetime->tm_wday;
    dt.hour = datetime->tm_hour;
    dt.min = datetime->tm_min;
    dt.sec = datetime->tm_sec;

    cyw43_arch_lwip_begin();
    rtc_init();
    rtc_set_datetime(&dt);
    cyw43_arch_lwip_end();
}

int main()
{
    stdio_init_all();

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    printf("System Clock Frequency is %d Hz\n", clock_get_hz(clk_sys));
    printf("USB Clock Frequency is %d Hz\n", clock_get_hz(clk_usb));
    // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks

    // Enable wifi station
    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, SNTP_SERVER);
    sntp_init(); // Initialize SNTP without setting sync mode

    // Wait for SNTP to synchronize time
    printf("Waiting for SNTP time synchronization...\n");
    sleep_ms(5000); // Allow some time for SNTP to fetch the time

    // Get the current time from the RTC
    datetime_t rtc_time;

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
        rtc_get_datetime(&rtc_time);
    
        // Convert RTC time to epoch timestamp
        time_t epoch_time = custom_mktime(&rtc_time);
        if (epoch_time == -1) {
            printf("Failed to convert RTC time to epoch timestamp.\n");
        } else {
            printf("Current RTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
                   rtc_time.year, rtc_time.month, rtc_time.day,
                   rtc_time.hour, rtc_time.min, rtc_time.sec);
            printf("Epoch timestamp: %lld\n", (long long)epoch_time);
        }
    
    }
}
