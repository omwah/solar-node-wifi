#include "nvs_store.h"

#include "nvs.h"

namespace bridge
{

namespace
{
const char *NAMESPACE = "cfg";
}

bool NvsStore::load(const char *key, std::string &value)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = 0;
    bool ok = nvs_get_str(h, key, nullptr, &len) == ESP_OK;
    if (ok) {
        std::string buf(len, '\0');
        ok = nvs_get_str(h, key, buf.data(), &len) == ESP_OK;
        if (ok) {
            buf.resize(len > 0 ? len - 1 : 0); // drop the terminator NVS counts
            value = buf;
        }
    }
    nvs_close(h);
    return ok;
}

bool NvsStore::save(const char *key, const std::string &value)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_str(h, key, value.c_str()) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

bool NvsStore::erase(const char *key)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

} // namespace bridge
