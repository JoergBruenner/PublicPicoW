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
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "pico/binary_info.h"

#define WIFI_SSID "ssid"
#define WIFI_PASSWORD "wlan key"
int count = 0;

//#define MQTT_PORT 1883
int mqtt_status = 0;

bool reserved_addr(uint8_t addr) {
    return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}

static void incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    printf("Topic %s, length: %d\n", topic, tot_len);
}

static void incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    char *payload = (char *)data;
    printf("payload\n%.*s\n", len, payload);
}

static void pub_request_cb(void *arg, err_t result)
{
    printf("Publish result %d\n", result);
    mqtt_client_t *client = (mqtt_client_t *)arg;
}

/* Called when publish is complete either with sucess or failure */
static void mqtt_pub_request_cb(void *arg, err_t result)
{
  if(result != ERR_OK) {
    printf("Publish result: %d\n", result);
  }
}

static void sub_request_cb(void *arg, err_t result)
{
  /* Just print the result code here for simplicity, 
     normal behaviour would be to take some action if subscribe fails like 
     notifying user, retry subscribe or disconnect from server */
  printf("Subscribe result: %d\n", result);
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {

  if (status == MQTT_CONNECT_ACCEPTED) {
    printf("MQTT_CONNECT_ACCEPTED");
    mqtt_status = 2;
  }
}

int main()
{
    stdio_init_all();

    ip_addr_t addr;
    if (!ip4addr_aton("192.168.2.227", &addr)) {
        printf("ip error\n");
    }
    else {
        if (cyw43_arch_init()) {
            printf("failed to initialise\n");
        }
        else {
            cyw43_arch_enable_sta_mode();
            if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
                printf("failed to connect\n");
            } else {
                printf("connected\n");
                cyw43_arch_lwip_begin();
                mqtt_client_t *client = mqtt_client_new();
                struct mqtt_connect_client_info_t ci;
                memset(&ci, 0, sizeof(ci));
                ci.client_id = "MyPicoW";
                ci.keep_alive = 10;
                err_t err3;
                err3 = mqtt_client_connect(client, &addr, 1883, mqtt_connection_cb, 0, &ci);
                cyw43_arch_lwip_end();
                if ( err3 == ERR_OK) {
                    u8_t qos;
                    u8_t retain;
                    while (true)
                    {   
                        switch (mqtt_status)
                        {
                            case 0: break;
                            case 1: {
                                cyw43_arch_lwip_begin();
                                mqtt_set_inpub_callback(client, incoming_publish_cb, incoming_data_cb, NULL);
                                err_t err2 = mqtt_subscribe(client, "test", 1, sub_request_cb, NULL);
                                cyw43_arch_lwip_end();
                                break;
                            }
                            case 2:{
                                qos = 2;
                                retain = 0;
                                cyw43_arch_lwip_begin();
                                err3 = mqtt_publish(client, "test", "test002", 7, qos, retain, pub_request_cb, client);
                                cyw43_arch_lwip_end();
                                break;
                            }
                            default:{
                            printf("State: %d", mqtt_status);
                            }
                        }
                        sleep_ms(500);
                        count += 1;
                    }
                }
                else {
                    printf("MQTT connection error.");
                }
            }
        }
    }
}
