#include "ota.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "psa/crypto.h"

#include <cstdio>
#include <cstring>

namespace bridge
{

namespace
{

const char *TAG = "ota";
constexpr uint64_t SLAVE_PROOF_MS = 15 * 60 * 1000;
constexpr uint64_t VERIFY_DEADLINE_MS = 30 * 60 * 1000;

std::string toHex(const uint8_t *bytes, size_t len)
{
    std::string out;
    char buf[3];
    for (size_t i = 0; i < len; i++) {
        snprintf(buf, sizeof(buf), "%02x", bytes[i]);
        out += buf;
    }
    return out;
}

std::string lower(std::string s)
{
    for (char &c : s) {
        if (c >= 'A' && c <= 'F') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

} // namespace

bool otaInstall(const std::string &url, const std::string &sha256Hex, std::string &error)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (!target) {
        error = "no OTA slot";
        return false;
    }

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    if (url.rfind("https://", 0) == 0) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http) {
        error = "http init failed";
        return false;
    }
    if (esp_http_client_open(http, 0) != ESP_OK) {
        esp_http_client_cleanup(http);
        error = "cannot reach " + url;
        return false;
    }
    int64_t length = esp_http_client_fetch_headers(http);
    int status = esp_http_client_get_status_code(http);
    if (status != 200) {
        esp_http_client_cleanup(http);
        error = "HTTP " + std::to_string(status);
        return false;
    }
    if (length > static_cast<int64_t>(target->size)) {
        esp_http_client_cleanup(http);
        error = "image larger than the OTA slot";
        return false;
    }

    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        esp_http_client_cleanup(http);
        error = "esp_ota_begin failed";
        return false;
    }
    psa_crypto_init();
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&hash, PSA_ALG_SHA_256);

    static uint8_t buf[1024];
    size_t total = 0;
    bool ok = true;
    for (;;) {
        int n = esp_http_client_read(http, reinterpret_cast<char *>(buf), sizeof(buf));
        if (n < 0) {
            error = "download failed";
            ok = false;
            break;
        }
        if (n == 0) {
            if (esp_http_client_is_complete_data_received(http)) {
                break;
            }
            error = "download incomplete";
            ok = false;
            break;
        }
        psa_hash_update(&hash, buf, n);
        if (esp_ota_write(ota, buf, n) != ESP_OK) {
            error = "flash write failed";
            ok = false;
            break;
        }
        total += n;
    }
    esp_http_client_cleanup(http);

    uint8_t digest[32] = {};
    size_t digestLen = 0;
    psa_hash_finish(&hash, digest, sizeof(digest), &digestLen);
    if (ok && lower(sha256Hex) != toHex(digest, digestLen)) {
        error = "SHA-256 mismatch";
        ok = false;
    }
    if (!ok) {
        esp_ota_abort(ota);
        ESP_LOGW(TAG, "update rejected after %u bytes: %s", static_cast<unsigned>(total), error.c_str());
        return false;
    }
    // esp_ota_end also validates the image header and its own appended checksum.
    if (esp_ota_end(ota) != ESP_OK) {
        error = "image failed validation";
        return false;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        error = "cannot select new image";
        return false;
    }
    ESP_LOGI(TAG, "installed %u bytes into %s; reboot to run it", static_cast<unsigned>(total), target->label);
    return true;
}

void OtaVerifier::begin(uint64_t bootMs)
{
    bootMs_ = bootMs;
    esp_ota_img_states_t state;
    pending_ = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
               state == ESP_OTA_IMG_PENDING_VERIFY;
    if (pending_) {
        ESP_LOGW(TAG, "new image pending verification");
    }
}

void OtaVerifier::update(uint64_t nowMs, bool nodeSeen, bool pollSucceeded)
{
    if (!pending_) {
        return;
    }
    if (nodeSeen || nowMs - bootMs_ >= SLAVE_PROOF_MS) {
        slaveProven_ = true;
    }
    if (slaveProven_ && pollSucceeded) {
        esp_ota_mark_app_valid_cancel_rollback();
        pending_ = false;
        ESP_LOGI(TAG, "new image verified");
        return;
    }
    if (nowMs - bootMs_ >= VERIFY_DEADLINE_MS) {
        ESP_LOGE(TAG, "new image not verified in time; rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

} // namespace bridge
