#pragma once

#include <cstdint>
#include <string>

namespace bridge
{

// Downloads a firmware image into the inactive OTA slot, hashing it as it streams, and
// selects it for the next boot only if the SHA-256 matches. Returns false with a reason.
bool otaInstall(const std::string &url, const std::string &sha256Hex, std::string &error);

// Rollback rule (docs/PLAN.md §4.5): a freshly installed image is "pending verify" until it
// has served the node (or kept the slave up for 15 min) and completed one successful poll.
// If that doesn't happen within 30 min, the previous image is restored.
class OtaVerifier
{
  public:
    void begin(uint64_t bootMs);
    void update(uint64_t nowMs, bool nodeSeen, bool pollSucceeded);
    bool pending() const { return pending_; }

  private:
    bool pending_ = false;
    bool slaveProven_ = false;
    uint64_t bootMs_ = 0;
};

} // namespace bridge
