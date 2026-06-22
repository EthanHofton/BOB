#include "wifi.h"
#include "balance.h"
#include "drv8833.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include <errno.h>
#include <string.h>

static const char *TAG = "wifi";

static httpd_handle_t s_httpd      = NULL;
static TaskHandle_t   s_stream_task = NULL;
static TaskHandle_t   s_tcp_task    = NULL;
static volatile bool  s_running     = false;

// ---- Soft AP ---------------------------------------------------------------

static void ap_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "Client joined  AID=%d  " MACSTR, e->aid, MAC2STR(e->mac));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGI(TAG, "Client left    AID=%d  " MACSTR, e->aid, MAC2STR(e->mac));
    }
}

static esp_err_t init_softap(void) {
    // NVS is required by the WiFi driver.
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_ERROR_CHECK(esp_netif_init());

    // esp_event_loop_create_default() returns ESP_ERR_INVALID_STATE if already
    // created — that is fine, just ignore it.
    esp_err_t el = esp_event_loop_create_default();
    if (el != ESP_OK && el != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(el);
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, ap_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .ap = {
            .ssid           = WIFI_AP_SSID,
            .ssid_len       = sizeof(WIFI_AP_SSID) - 1,
            .channel        = WIFI_AP_CHANNEL,
            .password       = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg        = { .required = false },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP ready — SSID=\"%s\"  pass=\"%s\"  ch=%d",
             WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL);
    return ESP_OK;
}

// ---- WebSocket telemetry stream --------------------------------------------

#define MAX_WS_CLIENTS 4
#define WS_FRAME_BUFSZ 384

static void ws_stream_task(void *arg) {
    httpd_handle_t server = (httpd_handle_t)arg;
    const TickType_t period = pdMS_TO_TICKS(1000 / WIFI_STREAM_HZ);

    char buf[WS_FRAME_BUFSZ];

    while (s_running) {
        balance_telemetry_t t;
        balance_get_telemetry(&t);

        int len = snprintf(buf, sizeof(buf),
            "{\"t\":%lld"
            ",\"roll\":%.3f,\"pitch\":%.3f"
            ",\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f"
            ",\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f"
            ",\"temp\":%.2f"
            ",\"pid_out\":%.3f"
            ",\"motor\":%lu}",
            (long long)t.timestamp_us,
            t.roll, t.pitch,
            t.accel_x, t.accel_y, t.accel_z,
            t.gyro_x,  t.gyro_y,  t.gyro_z,
            t.temp_c,
            t.pid_output,
            (unsigned long)t.motor_speed);

        size_t num = MAX_WS_CLIENTS;
        int fds[MAX_WS_CLIENTS];

        if (httpd_get_client_list(server, &num, fds) == ESP_OK) {
            for (size_t i = 0; i < num; i++) {
                if (httpd_ws_get_fd_info(server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
                    continue;

                httpd_ws_frame_t frame = {
                    .type    = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t *)buf,
                    .len     = (size_t)len,
                };
                // Async send — safe to call from outside an HTTP handler context.
                httpd_ws_send_frame_async(server, fds[i], &frame);
            }
        }

        vTaskDelay(period);
    }

    vTaskDelete(NULL);
}

// Upgrade handler: accept the WebSocket handshake then do nothing.
// All data flows outbound via ws_stream_task.
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket client connected");
        return ESP_OK;
    }

    // Drain any unexpected inbound frames.
    uint8_t scratch[64];
    httpd_ws_frame_t frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = scratch,
    };
    httpd_ws_recv_frame(req, &frame, sizeof(scratch));
    return ESP_OK;
}

static httpd_handle_t start_httpd(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port        = WIFI_HTTP_PORT;
    cfg.max_open_sockets   = MAX_WS_CLIENTS + 2; // +2 for normal HTTP headroom

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    static const httpd_uri_t ws_uri = {
        .uri          = WIFI_WS_PATH,
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_uri);

    ESP_LOGI(TAG, "WebSocket: ws://192.168.4.1:%d%s  @ %dHz",
             WIFI_HTTP_PORT, WIFI_WS_PATH, WIFI_STREAM_HZ);
    return server;
}

// ---- TCP tuning server -----------------------------------------------------

#define TCP_RXBUF_SIZE 256
#define TCP_TXBUF_SIZE 256

// Parse one JSON command line, write a JSON reply into out[0..out_size-1].
// Returns the number of bytes written (not including NUL), or -1 on error.
static int handle_command(const char *line, char *out, int out_size) {
    cJSON *root = cJSON_ParseWithLength(line, strlen(line));
    if (!root) {
        return snprintf(out, out_size,
                        "{\"status\":\"error\",\"message\":\"invalid JSON\"}");
    }

    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd) || !cmd->valuestring) {
        cJSON_Delete(root);
        return snprintf(out, out_size,
                        "{\"status\":\"error\",\"message\":\"missing cmd\"}");
    }

    int len = 0;

    if (strcmp(cmd->valuestring, "set_gains") == 0) {
        const cJSON *kp = cJSON_GetObjectItem(root, "kp");
        const cJSON *ki = cJSON_GetObjectItem(root, "ki");
        const cJSON *kd = cJSON_GetObjectItem(root, "kd");
        if (!cJSON_IsNumber(kp) || !cJSON_IsNumber(ki) || !cJSON_IsNumber(kd)) {
            len = snprintf(out, out_size,
                "{\"status\":\"error\",\"message\":\"set_gains needs kp, ki, kd\"}");
        } else {
            balance_set_gains((float)kp->valuedouble,
                              (float)ki->valuedouble,
                              (float)kd->valuedouble);
            len = snprintf(out, out_size, "{\"status\":\"ok\"}");
        }

    } else if (strcmp(cmd->valuestring, "set_setpoint") == 0) {
        const cJSON *val = cJSON_GetObjectItem(root, "value");
        if (!cJSON_IsNumber(val)) {
            len = snprintf(out, out_size,
                "{\"status\":\"error\",\"message\":\"set_setpoint needs value\"}");
        } else {
            balance_set_setpoint((float)val->valuedouble);
            len = snprintf(out, out_size, "{\"status\":\"ok\"}");
        }

    } else if (strcmp(cmd->valuestring, "get_params") == 0) {
        float kp, ki, kd, sp, ct;
        balance_get_params(&kp, &ki, &kd, &sp, &ct);
        len = snprintf(out, out_size,
            "{\"status\":\"ok\",\"params\":"
            "{\"kp\":%.4f,\"ki\":%.4f,\"kd\":%.4f,\"setpoint\":%.4f"
            ",\"coast\":%.4f}}",
            kp, ki, kd, sp, ct);

    } else if (strcmp(cmd->valuestring, "set_coast") == 0) {
        const cJSON *val = cJSON_GetObjectItem(root, "value");
        if (!cJSON_IsNumber(val)) {
            len = snprintf(out, out_size,
                "{\"status\":\"error\",\"message\":\"set_coast needs value (0.0…1.0)\"}");
        } else {
            balance_set_coast_threshold((float)val->valuedouble);
            len = snprintf(out, out_size, "{\"status\":\"ok\"}");
        }

    } else if (strcmp(cmd->valuestring, "reset_pid") == 0) {
        balance_reset_pid();
        len = snprintf(out, out_size, "{\"status\":\"ok\"}");

    } else if (strcmp(cmd->valuestring, "motor_manual") == 0) {
        const cJSON *en = cJSON_GetObjectItem(root, "enabled");
        if (!cJSON_IsBool(en)) {
            len = snprintf(out, out_size,
                "{\"status\":\"error\",\"message\":\"motor_manual needs enabled (bool)\"}");
        } else {
            balance_set_manual(cJSON_IsTrue(en));
            len = snprintf(out, out_size,
                "{\"status\":\"ok\",\"manual\":%s}",
                balance_is_manual() ? "true" : "false");
        }

    } else if (strcmp(cmd->valuestring, "motor_raw") == 0) {
        // value: -1.0…+1.0, bypasses deadpoint remap (for calibration)
        const cJSON *val = cJSON_GetObjectItem(root, "value");
        if (!cJSON_IsNumber(val)) {
            len = snprintf(out, out_size,
                "{\"status\":\"error\",\"message\":\"motor_raw needs value (-1.0…+1.0)\"}");
        } else {
            balance_manual_drive_raw((float)val->valuedouble);
            len = snprintf(out, out_size, "{\"status\":\"ok\"}");
        }

    } else if (strcmp(cmd->valuestring, "motor_speed") == 0) {
        // value: -1.0…+1.0, goes through deadpoint remap
        const cJSON *val = cJSON_GetObjectItem(root, "value");
        if (!cJSON_IsNumber(val)) {
            len = snprintf(out, out_size,
                "{\"status\":\"error\",\"message\":\"motor_speed needs value (-1.0…+1.0)\"}");
        } else {
            balance_manual_drive_speed((float)val->valuedouble);
            len = snprintf(out, out_size, "{\"status\":\"ok\"}");
        }

    } else if (strcmp(cmd->valuestring, "motor_coast") == 0) {
        balance_manual_coast();
        len = snprintf(out, out_size, "{\"status\":\"ok\"}");

    } else if (strcmp(cmd->valuestring, "set_deadpoint") == 0) {
        const cJSON *val = cJSON_GetObjectItem(root, "value");
        if (!cJSON_IsNumber(val)) {
            len = snprintf(out, out_size,
                "{\"status\":\"error\",\"message\":\"set_deadpoint needs value (0.0…1.0)\"}");
        } else {
            drv8833_set_deadpoint((float)val->valuedouble);
            len = snprintf(out, out_size,
                "{\"status\":\"ok\",\"deadpoint\":%.4f}", drv8833_get_deadpoint());
        }

    } else if (strcmp(cmd->valuestring, "get_deadpoint") == 0) {
        len = snprintf(out, out_size,
            "{\"status\":\"ok\",\"deadpoint\":%.4f}", drv8833_get_deadpoint());

    } else {
        len = snprintf(out, out_size,
            "{\"status\":\"error\",\"message\":\"unknown cmd\"}");
    }

    cJSON_Delete(root);
    return len;
}

static void tcp_server_task(void *arg) {
    char rx[TCP_RXBUF_SIZE];
    char tx[TCP_TXBUF_SIZE];

    int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv < 0) {
        ESP_LOGE(TAG, "TCP socket create failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Time out accept() every second so we can check s_running.
    struct timeval accept_tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &accept_tv, sizeof(accept_tv));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(WIFI_TCP_PORT),
    };
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "TCP bind failed: %d", errno);
        close(srv);
        vTaskDelete(NULL);
        return;
    }

    listen(srv, 1);
    ESP_LOGI(TAG, "TCP tuning server on port %d", WIFI_TCP_PORT);

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int conn = accept(srv, (struct sockaddr *)&client_addr, &addr_len);

        if (conn < 0) {
            // EAGAIN / EWOULDBLOCK from the recv timeout — just loop and check
            // s_running.
            continue;
        }

        ESP_LOGI(TAG, "TCP client connected: %s",
                 inet_ntoa(client_addr.sin_addr));

        // 60-second idle timeout on the client socket.
        struct timeval client_tv = { .tv_sec = 60, .tv_usec = 0 };
        setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &client_tv, sizeof(client_tv));

        int pos = 0;

        while (s_running) {
            int n = recv(conn, rx + pos, sizeof(rx) - pos - 1, 0);

            if (n == 0) {
                break; // clean close
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue; // idle timeout, keep waiting
                ESP_LOGW(TAG, "TCP recv error: %d", errno);
                break;
            }

            pos += n;
            rx[pos] = '\0';

            // Process every complete newline-terminated message in the buffer.
            char *start = rx;
            char *nl;
            while ((nl = strchr(start, '\n')) != NULL) {
                *nl = '\0';
                // Strip optional carriage return.
                char *cr = nl - 1;
                if (cr >= start && *cr == '\r')
                    *cr = '\0';

                if (*start != '\0') {
                    int tx_len = handle_command(start, tx, sizeof(tx) - 2);
                    if (tx_len > 0) {
                        tx[tx_len++] = '\n';
                        send(conn, tx, tx_len, 0);
                    }
                }
                start = nl + 1;
            }

            // Shift any partial message to the front of the buffer.
            int remaining = pos - (int)(start - rx);
            if (remaining > 0)
                memmove(rx, start, remaining);
            else
                remaining = 0;
            pos = remaining;

            // Guard against a line that is too long to fit the buffer.
            if (pos >= (int)(sizeof(rx) - 1)) {
                ESP_LOGW(TAG, "TCP rx overflow — clearing buffer");
                pos = 0;
            }
        }

        close(conn);
        ESP_LOGI(TAG, "TCP client disconnected");
    }

    close(srv);
    vTaskDelete(NULL);
}

// ---- Public API ------------------------------------------------------------

esp_err_t wifi_init(void) {
    ESP_ERROR_CHECK(init_softap());

    s_httpd = start_httpd();
    if (s_httpd == NULL)
        return ESP_FAIL;

    s_running = true;

    BaseType_t ret;
    ret = xTaskCreate(ws_stream_task, "ws_stream", 3072, s_httpd,
                      3, &s_stream_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WS stream task");
        return ESP_FAIL;
    }

    ret = xTaskCreate(tcp_server_task, "tcp_tune", 4096, NULL,
                      4, &s_tcp_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP server task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void wifi_stop(void) {
    s_running = false;
    // Give tasks a moment to notice s_running and self-delete.
    vTaskDelay(pdMS_TO_TICKS(1500));

    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    esp_wifi_stop();
    esp_wifi_deinit();
}
