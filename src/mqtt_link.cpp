#include "mqtt_link.h"

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "mqtt_client.h"

#include <cstring>

namespace bridge
{

namespace
{

const char *TAG = "mqtt";
constexpr EventBits_t CONNECTED = BIT0;
constexpr EventBits_t FAILED = BIT1;
constexpr EventBits_t AUTH_REJECTED = BIT2;
constexpr size_t MAX_MESSAGE = 4096;
constexpr int QUEUE_DEPTH = 4;

} // namespace

struct MqttLink::Impl {
    esp_mqtt_client_handle_t client = nullptr;
    EventGroupHandle_t events = xEventGroupCreate();
    QueueHandle_t telemetry = xQueueCreate(QUEUE_DEPTH, sizeof(std::string *));
    QueueHandle_t commands = xQueueCreate(QUEUE_DEPTH, sizeof(std::string *));
    std::string prefix;      // solarnode/<id>/
    std::string telemetryTopic;
    std::string commandTopic;
    std::string uri, clientId, user, pass;
    std::string partial;     // reassembly of a fragmented message
    std::string partialTopic;

    static void onEvent(void *arg, esp_event_base_t, int32_t id, void *data)
    {
        auto *self = static_cast<Impl *>(arg);
        auto *e = static_cast<esp_mqtt_event_handle_t>(data);
        switch (static_cast<esp_mqtt_event_id_t>(id)) {
        case MQTT_EVENT_CONNECTED:
            xEventGroupSetBits(self->events, CONNECTED);
            break;
        case MQTT_EVENT_DISCONNECTED:
            xEventGroupClearBits(self->events, CONNECTED);
            xEventGroupSetBits(self->events, FAILED);
            break;
        case MQTT_EVENT_ERROR:
            if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                esp_mqtt_connect_return_code_t rc = e->error_handle->connect_return_code;
                if (rc == MQTT_CONNECTION_REFUSE_BAD_USERNAME || rc == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED) {
                    xEventGroupSetBits(self->events, AUTH_REJECTED);
                }
            }
            xEventGroupSetBits(self->events, FAILED);
            break;
        case MQTT_EVENT_DATA:
            self->onData(e);
            break;
        default:
            break;
        }
    }

    void onData(esp_mqtt_event_handle_t e)
    {
        if (e->total_data_len > static_cast<int>(MAX_MESSAGE)) {
            ESP_LOGW(TAG, "dropping %d-byte message", e->total_data_len);
            return;
        }
        if (e->current_data_offset == 0) {
            partialTopic.assign(e->topic, e->topic_len);
            partial.clear();
            partial.reserve(e->total_data_len);
        }
        partial.append(e->data, e->data_len);
        if (static_cast<int>(partial.size()) < e->total_data_len) {
            return;
        }
        QueueHandle_t q = partialTopic == telemetryTopic ? telemetry : partialTopic == commandTopic ? commands : nullptr;
        if (!q) {
            return;
        }
        auto *msg = new std::string(std::move(partial));
        if (xQueueSend(q, &msg, 0) != pdTRUE) {
            // Keep the newest telemetry: drop the oldest queued copy and retry.
            std::string *old = nullptr;
            if (q == telemetry && xQueueReceive(q, &old, 0) == pdTRUE) {
                delete old;
                if (xQueueSend(q, &msg, 0) == pdTRUE) {
                    return;
                }
            }
            delete msg;
        }
        partial.clear();
    }

    static bool take(QueueHandle_t q, std::string &out, uint32_t waitMs)
    {
        std::string *msg = nullptr;
        if (xQueueReceive(q, &msg, pdMS_TO_TICKS(waitMs)) != pdTRUE) {
            return false;
        }
        out = std::move(*msg);
        delete msg;
        return true;
    }

    void drain(QueueHandle_t q)
    {
        std::string *msg = nullptr;
        while (xQueueReceive(q, &msg, 0) == pdTRUE) {
            delete msg;
        }
    }
};

MqttLink::MqttLink() : impl_(new Impl) {}

MqttLink::~MqttLink()
{
    disconnect();
    impl_->drain(impl_->telemetry);
    impl_->drain(impl_->commands);
    vQueueDelete(impl_->telemetry);
    vQueueDelete(impl_->commands);
    vEventGroupDelete(impl_->events);
    delete impl_;
}

ConnectResult MqttLink::connect(const settings::Settings &s, uint32_t timeoutMs)
{
    disconnect();
    Impl &m = *impl_;
    std::string id = s.str("node_id");
    m.prefix = "solarnode/" + id + "/";
    m.telemetryTopic = m.prefix + "telemetry";
    m.commandTopic = m.prefix + "cmd";
    bool tls = s.flag("mqtt_tls");
    m.uri = std::string(tls ? "mqtts://" : "mqtt://") + s.str("mqtt_host") + ":" + std::to_string(s.u32("mqtt_port"));
    m.clientId = "solarnode-" + id;
    m.user = s.str("mqtt_user");
    m.pass = s.str("mqtt_pass");

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = m.uri.c_str();
    if (tls) {
        cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
    cfg.credentials.client_id = m.clientId.c_str();
    if (!m.user.empty()) {
        cfg.credentials.username = m.user.c_str();
        cfg.credentials.authentication.password = m.pass.c_str();
    }
    cfg.session.disable_clean_session = true;
    cfg.session.keepalive = 60;
    cfg.network.disable_auto_reconnect = true;
    cfg.network.timeout_ms = 5000;
    cfg.buffer.size = 2048;

    xEventGroupClearBits(m.events, CONNECTED | FAILED | AUTH_REJECTED);
    m.client = esp_mqtt_client_init(&cfg);
    if (!m.client) {
        return ConnectResult::Failed;
    }
    esp_mqtt_client_register_event(m.client, MQTT_EVENT_ANY, Impl::onEvent, &m);
    if (esp_mqtt_client_start(m.client) != ESP_OK) {
        disconnect();
        return ConnectResult::Failed;
    }
    EventBits_t bits =
        xEventGroupWaitBits(m.events, CONNECTED | FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeoutMs));
    if (!(bits & CONNECTED)) {
        ConnectResult r = (bits & AUTH_REJECTED) ? ConnectResult::AuthRejected : ConnectResult::Failed;
        disconnect();
        return r;
    }
    esp_mqtt_client_subscribe_single(m.client, m.telemetryTopic.c_str(), 0);
    esp_mqtt_client_subscribe_single(m.client, m.commandTopic.c_str(), 1);
    return ConnectResult::Ok;
}

void MqttLink::disconnect()
{
    if (impl_->client) {
        esp_mqtt_client_disconnect(impl_->client);
        esp_mqtt_client_stop(impl_->client);
        esp_mqtt_client_destroy(impl_->client);
        impl_->client = nullptr;
    }
    xEventGroupClearBits(impl_->events, CONNECTED);
}

bool MqttLink::connected() const
{
    return impl_->client && (xEventGroupGetBits(impl_->events) & CONNECTED);
}

bool MqttLink::takeTelemetry(std::string &out, uint32_t waitMs)
{
    return Impl::take(impl_->telemetry, out, waitMs);
}

bool MqttLink::takeCommand(std::string &out, uint32_t waitMs)
{
    return Impl::take(impl_->commands, out, waitMs);
}

bool MqttLink::publish(const char *suffix, const std::string &payload, bool retain, int qos)
{
    if (!connected()) {
        return false;
    }
    std::string topic = impl_->prefix + suffix;
    return esp_mqtt_client_publish(impl_->client, topic.c_str(), payload.data(), static_cast<int>(payload.size()), qos,
                                   retain ? 1 : 0) >= 0;
}

} // namespace bridge
