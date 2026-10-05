#include <stdio.h>
#include <string.h>
#include <time.h>
#include "net_time.h"
#include "settings.h"
#include "board_periph.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

static const char *TAG = "net_time";

/* TEMP test network: remove by deleting wifi_test_credentials.h */
#if __has_include("wifi_test_credentials.h")
#include "wifi_test_credentials.h"
#define WIFI_SSID     TEST_WIFI_SSID
#define WIFI_PASSWORD TEST_WIFI_PASSWORD
#else
#define WIFI_SSID     CONFIG_HMI_WIFI_SSID
#define WIFI_PASSWORD CONFIG_HMI_WIFI_PASSWORD
#endif

static bool   s_wifi_on, s_got_ip, s_sntp_on, s_synced;
static time_t s_sync_time;
static char   s_ip[16];
static char   s_status[48];

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_got_ip = false;
        esp_wifi_connect();                       /* keep retrying */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_got_ip = true;
        ESP_LOGI(TAG, "Got IP %s", s_ip);
    }
}

static void time_synced(struct timeval *tv)
{
    s_synced = true;
    s_sync_time = tv->tv_sec;
    board_rtc_write(tv->tv_sec);                  /* keep the RTC in step */
    ESP_LOGI(TAG, "Time synchronized");
}

bool net_time_available(void) { return WIFI_SSID[0] != '\0'; }
bool net_link_up(void)        { return s_got_ip; }
const char *net_ip(void)      { return s_got_ip ? s_ip : ""; }

void net_time_init(void)
{
    if (!net_time_available()) {
        ESP_LOGI(TAG, "No Wi-Fi SSID configured (menuconfig -> Filter HMI)");
        return;
    }
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    ESP_LOGI(TAG, "Connecting to Wi-Fi \"%s\"", WIFI_SSID);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    s_wifi_on = true;

    net_time_apply(g_settings.auto_net_time);
}

void net_time_apply(bool enable)
{
    if (!s_wifi_on) return;
    if (enable && !s_sntp_on) {
        esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_HMI_NTP_SERVER);
        c.sync_cb = time_synced;
        if (esp_netif_sntp_init(&c) == ESP_OK) s_sntp_on = true;
    } else if (!enable && s_sntp_on) {
        esp_netif_sntp_deinit();
        s_sntp_on = false;
        s_synced = false;
    }
}

const char *net_time_status(void)
{
    if (!net_time_available())  return "No Wi-Fi configured";
    if (!s_got_ip)              return "Connecting to Wi-Fi...";
    if (!s_sntp_on)             return "Wi-Fi connected";
    if (!s_synced)              return "Waiting for time server...";
    struct tm tm;
    time_t t = s_sync_time + settings_utc_offset_s();
    gmtime_r(&t, &tm);
    snprintf(s_status, sizeof(s_status), "Synced at %02d:%02d", tm.tm_hour, tm.tm_min);
    return s_status;
}
