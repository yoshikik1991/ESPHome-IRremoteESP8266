#pragma once

#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/ir_remote_base/ir_remote_base.h"
#include "ir_Mitsubishi.h"

namespace esphome
{
    namespace mitsubishi
    {
        enum Model
        {
            MITSUBISHI_AC,
            MITSUBISHI136,
            MITSUBISHI112,
        };

        class MitsubishiClimate : public ir_remote_base::IrRemoteBase
        {
        public:
            MitsubishiClimate()
                : IrRemoteBase(kMitsubishiAcMinTemp, kMitsubishiAcMaxTemp, 1.0f, true, true,
                               {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH, climate::CLIMATE_FAN_QUIET},
                               {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH}) {}

            void set_model(const Model model);
            void setup() override;

            /// Thin passthrough to IRMitsubishiAC's own ISee (comfort/motion
            /// sensor feedback) field, not otherwise exposed by this component.
            /// Byte 6 bit 6 in a real-remote capture: Cool+ISee-off (0x18, "冷房
            /// (体感切)") is what the remote's own "weak cool"-style preset button
            /// sends, vs. a plain Cool selection's Cool+ISee-on (0x58, "冷房(体感
            /// 入)"). Combine with a normal climate mode/temperature/fan call to
            /// reproduce that preset instead of a bespoke raw-frame method.
            void set_isee(const bool isee);
            bool get_isee() const { return this->ac_.getISee(); }

            /// Internal clean function. Not exposed by IRMitsubishiAC at all -- byte
            /// 14 bit 2 is reverse-engineered from a real-remote capture (unused by
            /// the library's own Mitsubishi144Protocol bitfield layout), mirroring
            /// how fujitsu_264 pokes bits the underlying library doesn't model.
            /// UNVERIFIED on real hardware beyond the byte-level capture.
            void set_clean(const bool clean);
            bool get_clean() const { return this->clean_; }

            /// Dry mode strength, 0 (weak/"弱") .. 2 (strong/"強"), 1 = normal.
            /// Also has no field in IRMitsubishiAC; byte 8's low nibble is unused by
            /// the library (only the high nibble is WideVane), reverse-engineered
            /// from a real-remote capture. UNVERIFIED on real hardware beyond the
            /// byte-level capture.
            void set_dry_level(const uint8_t level);
            uint8_t get_dry_level() const { return this->dry_level_; }

            /// Sync state from a frame captured by ESPHome's built-in on_raw
            /// pulse dump (this protocol has no ESPHome built-in decoder, unlike
            /// fujitsu_264's on_aeha). Only implemented for Model::MITSUBISHI_AC.
            /// Returns true if a valid frame was decoded and applied.
            bool update_from_raw(const std::vector<int32_t> &pulses);

            /// Sync state from a frame captured by ESPHome's built-in AEHA decoder.
            /// This unit's remote also sends short AEHA-timed toggle codes (dry
            /// level cycle / internal clean button) separately from the main
            /// MITSUBISHI_AC state frame handled by update_from_raw(). Unlike
            /// fujitsu_264, this address does not need bit-reversal (confirmed by
            /// the pre-existing working `aeha:` binary_sensor matcher this method
            /// replaces). Returns true if state was updated.
            bool update_from_aeha(const uint16_t address, const std::vector<uint8_t> &data);

        protected:
            void transmit_state() override;

        private:
            void send();
            void apply_state();
            void apply_state_ac();
            void apply_state_136();
            void apply_state_112();

            Model model_ = Model::MITSUBISHI_AC;
            IRMitsubishiAC ac_ = IRMitsubishiAC(255); // pin is not used
            IRMitsubishi136 ac_136_ = IRMitsubishi136(255);
            IRMitsubishi112 ac_112_ = IRMitsubishi112(255);

            bool clean_ = false;
            uint8_t dry_level_ = 1;
        };

    } // namespace mitsubishi
} // namespace esphome
