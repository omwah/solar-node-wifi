#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace settings
{

enum class Type { String, Uint, Float, Bool };

struct KeyInfo {
    const char *name;
    Type type;
    const char *defaultValue;
    bool secret; // write-only: reads report only whether it is set
    double min;  // numeric types only
    double max;
    const char *help;
};

// Persistent key/value storage; NVS on the device, memory in host tests.
class Store
{
  public:
    virtual ~Store() {}
    virtual bool load(const char *key, std::string &value) = 0;
    virtual bool save(const char *key, const std::string &value) = 0;
    virtual bool erase(const char *key) = 0;
};

enum class SetResult { Ok, UnknownKey, BadValue, OutOfRange };

// The configuration model shared by the serial console and MQTT management.
// set() stages a change; commit() writes staged changes to the store.
class Settings
{
  public:
    static const KeyInfo *keys(size_t &count);
    static const KeyInfo *find(const char *name);

    explicit Settings(Store &store);

    void load();
    SetResult set(const char *name, const std::string &value);
    bool commit();
    bool dirty() const { return dirty_; }
    void factoryReset();

    // Value as text, with secrets redacted to "<set>" or "".
    std::string display(const char *name) const;

    std::string str(const char *name) const;
    uint32_t u32(const char *name) const;
    float f32(const char *name) const;
    bool flag(const char *name) const;

    // WiFi, broker and node id present: enough to leave commissioning.
    bool complete() const;

  private:
    int index(const char *name) const;

    Store &store_;
    std::string values_[32];
    bool changed_[32] = {};
    bool dirty_ = false;
};

const char *setResultName(SetResult result);

} // namespace settings
