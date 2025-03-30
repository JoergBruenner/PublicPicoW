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

#include "pico/stdlib.h"
#include <stdio.h>
#include "pico/sleep.h" // Correct include path for pico_sleep
#include "pico/cyw43_arch.h"
#include "hardware/rtc.h" // Include RTC header for datetime_t
#include <time.h> // Include for struct timespec

#define LED_PIN 15 

static bool awake;

static void sleep_callback(void) {
    printf("RTC woke us up\n");
    awake = true;
}

static void rtc_sleep(void) {
    // Start on Friday 5th of June 2020 15:45:00
    datetime_t t = {
            .year  = 2025,
            .month = 03,
            .day   = 30,
            .dotw  = 0, // 0 is Sunday, so 5 is Friday
            .hour  = 15,
            .min   = 00,
            .sec   = 00
    };

    // Alarm 10 seconds later
    datetime_t t_alarm = {
            .year  = 2025,
            .month = 03,
            .day   = 30,
            .dotw  = 0, // 0 is Sunday, so 5 is Friday
            .hour  = 15,
            .min   = 00,
            .sec   = 10
    };

    // Start the RTC
    rtc_init();
    rtc_set_datetime(&t);

    printf("Sleeping for 10 seconds\n");
    uart_default_tx_wait_blocking();
    printf("TX Wait blocking finished");

    // Wait for the RTC alarm
    rtc_set_alarm(&t_alarm, &sleep_callback);

    printf("Woke up from sleep!\n");
}

void sleep_now(void) {
    printf("Switching to XOSC\n");
    // Wait for the fifo to be drained so we get reliable output
    uart_default_tx_wait_blocking();
    printf("Got past blocking");
    // UART will be reconfigured by sleep_run_from_xosc
    sleep_run_from_xosc();
    printf("Switched to XOSC\n");

    awake = false;
    printf("Awake is %d\n", awake);
    
    rtc_sleep();

    // Make sure we don't wake
    while (!awake) {
        printf("Sleeping\n");
    }
    stdio_init_all();
    printf("Waked up.\n");
}

int main() {
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    printf("Hello Sleep!\n");
    while (1) {
        gpio_put(LED_PIN, 0);
        sleep_now(); // Call the sleep function
        gpio_put(LED_PIN, 1);
        sleep_ms(2500);
    }
    
    return 0;
}

