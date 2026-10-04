#pragma once

#include "measurement.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace senxx
{

enum class Model { SEN66, SEN55 };

enum class SensorState { Idle, Measuring, RhtGasOnly };

// Sensirion SEN66/SEN55 I²C slave behaviour, transport-independent.
//
// The transport calls onWrite() with every completed master write (after STOP) and
// serves response() when the master reads. Each write replaces the staged response,
// so bytes left over from a short read can never leak into the next reply.
//
// Threading: onWrite()/response() run in the I²C interrupt context; setMeasurement()
// runs in a task. setMeasurement() fills the inactive buffer and swaps an atomic
// index, so a reply is always built from one complete snapshot. This relies on the
// interrupt preempting the task and not vice versa (single core).
class Emulator
{
  public:
    static constexpr size_t MAX_RESPONSE = 48;

    explicit Emulator(Model model);

    Model model() const { return model_; }
    uint8_t address() const;

    void setMeasurement(const Measurement &m);

    void onWrite(const uint8_t *data, size_t len);
    const uint8_t *response() const { return response_; }
    size_t responseLength() const { return responseLen_; }

    SensorState state() const { return state_; }
    uint32_t transactionCount() const { return transactions_; }

  private:
    void handleCommand(uint16_t cmd, const uint8_t *args, size_t argBytes);
    void stageWords(const uint16_t *words, size_t count);
    void stageBytes(const uint8_t *bytes, size_t count);
    void stageProductName();
    void stageVersion();
    void stageMeasuredValues(const Measurement &m);
    void stagePmAndNumberConcentrations(const Measurement &m);
    void stageNumberConcentrations(const Measurement &m);

    Model model_;
    Measurement buffers_[2];
    std::atomic<int> active_{0};

    uint8_t response_[MAX_RESPONSE] = {};
    size_t responseLen_ = 0;

    SensorState state_ = SensorState::Idle;
    uint8_t vocState_[8] = {};
    uint32_t transactions_ = 0;
};

} // namespace senxx
