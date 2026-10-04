#pragma once

#include "settings.h"

namespace bridge
{

// settings::Store backed by the "cfg" NVS namespace.
class NvsStore : public settings::Store
{
  public:
    bool load(const char *key, std::string &value) override;
    bool save(const char *key, const std::string &value) override;
    bool erase(const char *key) override;
};

} // namespace bridge
