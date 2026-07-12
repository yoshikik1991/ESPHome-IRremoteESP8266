#pragma once

#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/ir_remote_base/ir_remote_base.h"
#include "ir_Panasonic.h"

namespace esphome
{
    namespace panasonic
    {
        enum Model
        {
            LKE = panasonic_ac_remote_model_t::kPanasonicLke,
            NKE = panasonic_ac_remote_model_t::kPanasonicNke,
            DKE = panasonic_ac_remote_model_t::kPanasonicDke,
            JKE = panasonic_ac_remote_model_t::kPanasonicJke,
            CKP = panasonic_ac_remote_model_t::kPanasonicCkp,
            RKR = panasonic_ac_remote_model_t::kPanasonicRkr
        };

        class PanasonicClimate : public ir_remote_base::IrRemoteBase
        {
        public:
            PanasonicClimate()
                : IrRemoteBase(kPanasonicAcMinTemp, kPanasonicAcMaxTemp, 1.0f, true, true,
                               {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH, climate::CLIMATE_FAN_QUIET},
                               {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

            void set_model(const Model model);
            void setup() override;
            climate::ClimateTraits traits() override;

            /// Sync state from a frame captured by ESPHome's built-in AEHA decoder
            /// (e.g. the real remote was used). raw_address/raw_data are
            /// bit-reversed by that decoder relative to the wire encoding; this
            /// un-reverses them internally, on the (unverified) assumption that
            /// this protocol is also LSB-first on the wire like fujitsu_264 --
            /// mirrors that component's own update_from_aeha() exactly. This
            /// protocol's real frame is 2 AEHA sections (8 bytes + 19 bytes,
            /// separated by a ~10ms gap that normally splits them into separate
            /// receive events at remote_receiver's default idle threshold): a
            /// lone 8-byte section 1 carries no climate state and is just logged;
            /// a lone 19-byte section 2 has the constant 8-byte section-1 prefix
            /// prepended before use; a 27-byte frame (both sections decoded as
            /// one, e.g. with a shorter idle threshold) is used directly.
            /// UNVERIFIED on real hardware -- compile-tested only. No physical
            /// unit of this protocol family was available; implemented by
            /// mirroring the verified fujitsu_264/mitsubishi receive paths.
            /// Returns true if state was updated.
            bool update_from_aeha(const uint16_t raw_address, const std::vector<uint8_t> &raw_data);

        protected:
            void transmit_state() override;
            void apply_state();
            bool on_receive(remote_base::RemoteReceiveData data) override;

            IRPanasonicAc ac_ = IRPanasonicAc(255); // pin is not used
        };

    } // namespace panasonic
} // namespace esphome
