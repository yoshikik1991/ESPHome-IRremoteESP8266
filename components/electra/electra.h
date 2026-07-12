#pragma once

#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/ir_remote_base/ir_remote_base.h"
#include "ir_Electra.h"

namespace esphome
{
    namespace electra
    {

        class ElectraClimate : public ir_remote_base::IrRemoteBase
        {
        public:
            ElectraClimate()
                : IrRemoteBase(kElectraAcMinTemp, kElectraAcMaxTemp, 1.0f, true, true,
                               {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
                               {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH}) {}

            void setup() override;

            /// Sync state from ESPHome's built-in on_raw pulse dump. This
            /// protocol has no ESPHome built-in decoder and isn't AEHA-timed
            /// (its leader is 9166/4470 vs. AEHA's 3400/1700), unlike
            /// fujitsu_264's on_aeha -- mirrors mitsubishi's own
            /// update_from_raw() (decode_pulses-based) but without an AEHA
            /// fallback, since this protocol has no AEHA-timed side-channel
            /// frames to worry about. Electra frames may arrive with repeats;
            /// decode_pulses() stops at the first pulse pair that doesn't fit
            /// the expected bit timing, so a single clean frame is what's
            /// expected here.
            /// UNVERIFIED on real hardware -- compile-tested only. No physical
            /// unit of this protocol family was available; implemented by
            /// mirroring the verified fujitsu_264/mitsubishi receive paths.
            /// Returns true if a valid frame was decoded and applied.
            bool update_from_raw(const std::vector<int32_t> &pulses);

        protected:
            void transmit_state() override;
            bool on_receive(remote_base::RemoteReceiveData data) override;

        private:
            void send();
            void apply_state();

            IRElectraAc ac_ = IRElectraAc(255); // pin is not used
        };

    } // namespace electra
} // namespace esphome
