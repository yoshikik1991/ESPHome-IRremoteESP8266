#pragma once

#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/optional.h"
#include "esphome/components/ir_remote_base/ir_remote_base.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
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

            /// Fixed vertical louver position, using IRMitsubishiAC's own native
            /// Vane field (fully supported by the library, unlike fujitsu_264's
            /// angle fields): 0 (auto) .. 5 (lowest fixed position), matching
            /// kMitsubishiAcVaneAuto/Highest/High/Middle/Low/Lowest. Selecting a
            /// position (including auto) stops continuous vertical swing --
            /// swing on/off itself is still controlled by the climate's own
            /// swing_mode, same split as fujitsu_264's set_vertical_angle()/
            /// swing_mode.
            void set_vertical_vane(const uint8_t position);
            uint8_t get_vertical_vane() const { return this->vertical_vane_; }

            /// Powerful (boost) mode. Not exposed by IRMitsubishiAC at all --
            /// byte 15 bit 4 is reverse-engineered from a real-remote capture
            /// (a gap between the library's own DirectIndirect/AbsenseDetect
            /// and iSave10C fields in that byte). Unlike fujitsu_264's
            /// toggle_powerful() (a stateless toggle command), this unit's
            /// remote sends it as a persistent flag within the normal state
            /// frame -- confirmed via two real captures (ON/OFF) that differed
            /// in only this bit -- so it's a plain set/get, and (like
            /// set_dry_level()/set_vertical_vane()) transmits immediately.
            void set_powerful(const bool powerful);
            bool get_powerful() const { return this->powerful_; }

            /// Current-limit ("電流切換") mode: true = limited ("小", caps the
            /// max operating current for households with a weak breaker),
            /// false = normal ("通常"). Not exposed by IRMitsubishiAC at all --
            /// byte 6 bit 2 (in the mode byte's unused low 3 bits) is
            /// reverse-engineered from real-remote captures: pressing the
            /// remote's 電流切換 button in Heat changed only this bit
            /// (0x08 -> 0x0C), and the same bit tracked the setting in Cool
            /// (0x18 -> 0x1C). Like set_powerful() it's a persistent flag
            /// within the normal state frame (the remote's LCD shows the
            /// setting permanently), not a stateless toggle code, so it's a
            /// plain set/get that transmits immediately.
            void set_current_cut(const bool current_cut);
            bool get_current_cut() const { return this->current_cut_; }

            /// Batch-apply any combination of standard climate fields and this
            /// unit's own extras (vertical vane, dry level, powerful, current
            /// cut, clean) from a single command, then transmit at most once.
            /// Unlike calling the individual set_*()/climate control APIs
            /// back-to-back -- each of which transmits immediately on its own
            /// -- this collects every provided field into internal state
            /// first. Fields left as nullopt keep their current value.
            /// mode/fan_mode/swing_mode are validated against this->traits()
            /// (an unsupported value is logged and that field alone is
            /// skipped; other fields still apply); target_temperature is
            /// clamped to the traits' visual min/max rather than rejected.
            /// Intended for the MQTT batch-command topic.
            void apply_batch(optional<climate::ClimateMode> mode,
                              optional<float> target_temperature,
                              optional<climate::ClimateFanMode> fan_mode,
                              optional<climate::ClimateSwingMode> swing_mode,
                              optional<uint8_t> vertical_vane,
                              optional<uint8_t> dry_level,
                              optional<bool> powerful,
                              optional<bool> current_cut,
                              optional<bool> clean);

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

            SUB_SELECT(dry_level)
            SUB_SELECT(vertical_vane)
            SUB_SWITCH(powerful)
            SUB_SWITCH(current_cut)
            SUB_SWITCH(internal_clean)

            /// Restrict the exposed climate modes/swing modes below what the
            /// library/model otherwise supports, for units that don't have a
            /// given feature at all (e.g. no auto/fan-only mode, no horizontal
            /// swing). Unset (the default) means "use the library/model
            /// default" -- these only ever narrow, never widen, what
            /// Model::MITSUBISHI_AC etc. already expose.
            void set_supports_auto(bool supports) { this->supports_auto_override_ = supports; }
            void set_supports_fan_only(bool supports) { this->supports_fan_only_override_ = supports; }
            void set_horizontal_swing_supported(bool supported) { this->horizontal_swing_override_ = supported; }
            void set_supports_quiet_fan(bool supports) { this->supports_quiet_fan_override_ = supports; }

        protected:
            void control(const climate::ClimateCall &call) override;
            void transmit_state() override;
            climate::ClimateTraits traits() override;
            bool on_receive(remote_base::RemoteReceiveData data) override;

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
            uint8_t vertical_vane_ = 0;
            bool powerful_ = false;
            bool current_cut_ = false;

            optional<bool> supports_auto_override_;
            optional<bool> supports_fan_only_override_;
            optional<bool> horizontal_swing_override_;
            optional<bool> supports_quiet_fan_override_;
        };

    } // namespace mitsubishi
} // namespace esphome
