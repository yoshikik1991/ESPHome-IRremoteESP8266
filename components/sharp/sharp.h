#pragma once

#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/ir_remote_base/ir_remote_base.h"
#include "ir_Sharp.h"

namespace esphome
{
    namespace sharp
    {
        enum Model
        {
            A907 = sharp_ac_remote_model_t::A907,
            A705 = sharp_ac_remote_model_t::A705,
            A903 = sharp_ac_remote_model_t::A903,
        };

        class SharpClimate : public ir_remote_base::IrRemoteBase
        {
        public:
            SharpClimate()
                : IrRemoteBase(kSharpAcMinTemp, kSharpAcMaxTemp, 1.0f, true, true,
                               {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
                               {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

            void set_model(const Model model);

            void setup() override;

            /// Sync state from ESPHome's built-in on_raw pulse dump. This
            /// protocol has no ESPHome built-in decoder and isn't AEHA-timed
            /// (its leader is 3800/1900 vs. AEHA's 3400/1700), same reasoning as
            /// electra's own update_from_raw().
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

            IRSharpAc ac_ = IRSharpAc(255); // pin is not used
        };

    } // namespace sharp
} // namespace esphome
