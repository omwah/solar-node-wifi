#include "net.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"

#include <cstring>
#include <ctime>

namespace bridge
{

namespace
{

const char *TAG = "net";
constexpr EventBits_t GOT_IP = BIT0;
constexpr EventBits_t FAILED = BIT1;
// Any time after this is a set clock (the RTC starts at 1970 after power-up).
constexpr time_t VALID_AFTER = 1700000000;

EventGroupHandle_t events = nullptr;
esp_netif_t *netif = nullptr;
bool started = false;
bool sntpStarted = false;

void onEvent(void *, esp_event_base_t base, int32_t id, void *)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(events, FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events, GOT_IP);
    }
}

bool applyStaticIp(const settings::Settings &s)
{
    std::string ip = s.str("ip");
    if (ip.empty()) {
        esp_netif_dhcpc_start(netif); // harmless if already running
        return true;
    }
    esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t info = {};
    info.ip.addr = ipaddr_addr(ip.c_str());
    info.gw.addr = ipaddr_addr(s.str("gateway").c_str());
    info.netmask.addr = ipaddr_addr(s.str("netmask").c_str());
    if (esp_netif_set_ip_info(netif, &info) != ESP_OK) {
        return false;
    }
    std::string dns = s.str("dns");
    if (!dns.empty()) {
        esp_netif_dns_info_t d = {};
        d.ip.u_addr.ip4.addr = ipaddr_addr(dns.c_str());
        d.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &d);
    }
    // With a static address no IP event fires; connecting is enough.
    return true;
}

} // namespace

void netInit()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr));
}

bool wifiConnect(const settings::Settings &s, uint32_t timeoutMs)
{
    wifi_config_t wc = {};
    std::string ssid = s.str("wifi_ssid");
    std::string pass = s.str("wifi_pass");
    std::strncpy(reinterpret_cast<char *>(wc.sta.ssid), ssid.c_str(), sizeof(wc.sta.ssid));
    std::strncpy(reinterpret_cast<char *>(wc.sta.password), pass.c_str(), sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    xEventGroupClearBits(events, GOT_IP | FAILED);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) {
        return false;
    }
    bool staticIp = !s.str("ip").empty();
    if (!applyStaticIp(s)) {
        ESP_LOGW(TAG, "bad static IP settings");
        return false;
    }
    if (esp_wifi_start() != ESP_OK) {
        return false;
    }
    started = true;

    if (staticIp) {
        // Wait for association only.
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
        wifi_ap_record_t ap;
        while (xTaskGetTickCount() < deadline) {
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(events, GOT_IP, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeoutMs));
    return (bits & GOT_IP) != 0;
}

void wifiStop()
{
    if (!started) {
        return;
    }
    if (sntpStarted) {
        esp_netif_sntp_deinit();
        sntpStarted = false;
    }
    esp_wifi_disconnect();
    esp_wifi_stop();
    started = false;
}

int wifiRssi()
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

bool sntpSync(const char *server, uint32_t timeoutMs)
{
    if (!sntpStarted) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
        if (esp_netif_sntp_init(&cfg) != ESP_OK) {
            return false;
        }
        sntpStarted = true;
    }
    return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeoutMs)) == ESP_OK || clockValid();
}

bool clockValid()
{
    return time(nullptr) > VALID_AFTER;
}

uint32_t epochNow()
{
    time_t now = time(nullptr);
    return now > VALID_AFTER ? static_cast<uint32_t>(now) : 0;
}

} // namespace bridge
