/*
 * Copyright (c) 2023 Gerhard G.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "pico-w-wifi-configure.h"

#define TCP_PORT 80
#define DEBUG_printf printf
#define BUF_SIZE 2048
#define MAX_URL_PARAMS 5

typedef struct {
    char* url;
    char* params[MAX_URL_PARAMS][2];
    int param_count;
} http_request_t;

typedef struct {
    http_request_t request;
    char http_request_buffer[BUF_SIZE];
    int http_request_len;
    wifi_config_t* wifi_config;
} http_connection_t;

// Note: This is a very basic URL parameter parser and does not handle URL decoding.
static int parse_url_params(char* query, http_request_t* request) {
    request->param_count = 0;
    char* param = strtok(query, "&");
    while (param != NULL && request->param_count < MAX_URL_PARAMS) {
        char* value = strchr(param, '=');
        if (value != NULL) {
            *value = '\0';
            request->params[request->param_count][0] = param;
            request->params[request->param_count][1] = value + 1;
            request->param_count++;
        }
        param = strtok(NULL, "&");
    }
    return request->param_count;
}


static err_t http_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    http_connection_t* con = (http_connection_t*)arg;
    tcp_close(tpcb);
    free(con);
    return ERR_OK;
}

static err_t http_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    http_connection_t* con = (http_connection_t*)arg;
    if (p == NULL) { // Connection closed
        tcp_close(tpcb);
        free(con);
        return ERR_OK;
    }

    // Extract HTTP request from pbuf
    if (con->http_request_len + p->tot_len <= BUF_SIZE) {
        pbuf_copy_partial(p, con->http_request_buffer + con->http_request_len, p->tot_len, 0);
        con->http_request_len += p->tot_len;
    }
    pbuf_free(p);

    // Check if we have the full HTTP header
    if (strstr(con->http_request_buffer, "\r\n\r\n")) {
        char* request_line = strtok(con->http_request_buffer, "\r\n");
        if (request_line) {
            char* method = strtok(request_line, " ");
            char* url = strtok(NULL, " ");

            if (strcmp(method, "GET") == 0) {
                char* query = strchr(url, '?');
                if (query != NULL) {
                    *query = '\0';
                    query++;
                    parse_url_params(query, &con->request);
                    for (int i = 0; i < con->request.param_count; i++) {
                        if (strcmp(con->request.params[i][0], "ssid") == 0) {
                            strncpy(con->wifi_config->ssid, con->request.params[i][1], WIFI_SSID_MAX_CHARS);
                            con->wifi_config->ssid[WIFI_SSID_MAX_CHARS] = '\0';
                        } else if (strcmp(con->request.params[i][0], "password") == 0) {
                            strncpy(con->wifi_config->password, con->request.params[i][1], WIFI_PASSWORD_MAX_CHARS);
                            con->wifi_config->password[WIFI_PASSWORD_MAX_CHARS] = '\0';
                        }
                    }
                }
            }
        }

        // Generate response
        char html[512];
        char response[600];
        if (con->wifi_config->ssid[0] != '\0') {
            snprintf(html, sizeof(html), "<html><body><h1>WiFi configured!</h1><p>SSID: %s</p><p>You can close this window.</p></body></html>", con->wifi_config->ssid);
        } else {
            snprintf(html, sizeof(html), "<html><body><form action='/' method='get'>SSID: <input type='text' name='ssid'><br>Password: <input type='password' name='password'><br><input type='submit' value='Submit'></form></body></html>");
        }
        int html_len = strlen(html);
        int response_len = snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            html_len, html);
        tcp_sent(tpcb, http_server_sent);
        err_t wr_err = tcp_write(tpcb, response, response_len, TCP_WRITE_FLAG_COPY);
        if (wr_err != ERR_OK) {
            printf("tcp_write failed: %d\n", wr_err);
        }
        tcp_output(tpcb);
    }
    return ERR_OK;
}

static err_t http_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    http_connection_t* con = (http_connection_t*)malloc(sizeof(http_connection_t));
    if (!con) return ERR_MEM;
    
    memset(con, 0, sizeof(http_connection_t));
    con->wifi_config = (wifi_config_t*)arg;

    tcp_arg(newpcb, con);
    tcp_recv(newpcb, http_server_recv);
    return ERR_OK;
}

int run_wifi_config_server(wifi_config_t* config) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        return -1;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, TCP_PORT);
    if (err != ERR_OK) {
        DEBUG_printf("failed to bind to port %u: %d\n", TCP_PORT, err);
        tcp_close(pcb);
        return -1;
    }

    pcb = tcp_listen(pcb);
    tcp_arg(pcb, config);
    tcp_accept(pcb, http_server_accept);

    printf("WiFi configuration server running on http://%s:%d\n", ip4addr_ntoa(netif_ip4_addr(netif_default)), TCP_PORT);

    // Wait for configuration to be submitted
    while (config->ssid[0] == '\0') {
        cyw43_arch_poll();
        sleep_ms(100);
    }

    tcp_close(pcb);
    
    return 0;
}