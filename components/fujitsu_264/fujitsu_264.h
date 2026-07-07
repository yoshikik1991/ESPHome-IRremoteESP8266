#pragma once

#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/ir_remote_base/ir_remote_base.h"
#include "ir_Fujitsu.h"

namespace esphome
{
    namespace fujitsu_264
    {
        class Fujitsu264Climate : public ir_remote_base::IrRemoteBase
        {
        public:
            Fujitsu264Climate()
                : IrRemoteBase(kFujitsuAc264MinTemp, kFujitsuAc264MaxTemp, 0.5f, true, true,
                               {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH, climate::CLIMATE_FAN_QUIET},
                               {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

            void setup() override;

            void set_fan_angle(const uint8_t fan_angle);
            void toggle_powerful();
            void set_clean(const bool clean);
            void toggle_sterilization();
            void set_weak_dry(const bool weak_dry);

            /// Sync state from a frame captured by ESPHome's built-in AEHA decoder
            /// (e.g. the real remote was used). raw_address/raw_data are bit-reversed
            /// by that decoder relative to the wire encoding; this un-reverses them
            /// internally. Returns true if state was updated.
            bool update_from_aeha(const uint16_t raw_address, const std::vector<uint8_t> &raw_data);
            bool get_weak_dry() const { return this->weak_dry_; }
            bool get_clean() const { return this->ac_.getClean(); }

        protected:
            void transmit_state() override;

        private:
            void send();
            void apply_state();

            IRFujitsuAC264 ac_ = IRFujitsuAC264(255); // pin is not used
            bool weak_dry_ = false;

            // Timestamp of our last transmission, so update_from_aeha() can ignore
            // our own signal bouncing back into the IR receiver.
            uint32_t last_tx_ms_ = 0;
        };
    } // namespace fujitsu_264
} // namespace esphome
