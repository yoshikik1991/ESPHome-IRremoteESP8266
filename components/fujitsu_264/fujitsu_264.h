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
            // Custom fan mode labels matching the real remote's own terms (rather
            // than ESPHome's generic Low/Medium/High), requested so the HA UI
            // reads the same as the physical remote.
            static constexpr const char *kFanModeAuto = "自動";
            static constexpr const char *kFanModeQuiet = "静音";
            static constexpr const char *kFanModeLow = "微風";
            static constexpr const char *kFanModeMedium = "弱風";
            static constexpr const char *kFanModeHigh = "強風";

            Fujitsu264Climate()
                : IrRemoteBase(kFujitsuAc264MinTemp, kFujitsuAc264MaxTemp, 0.5f, true, true,
                               {},
                               {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL,
                                climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH})
            {
                this->set_supported_custom_fan_modes(
                    {kFanModeAuto, kFanModeQuiet, kFanModeLow, kFanModeMedium, kFanModeHigh});
            }

            void setup() override;

            void set_fan_angle(const uint8_t fan_angle);
            void toggle_powerful();
            void set_clean(const bool clean);
            void toggle_sterilization();
            void set_weak_dry(const bool weak_dry);

            /// Fixed vertical (up/down) louver position, 1 (up) .. 8 (down).
            /// Reverse-engineered from a real-remote capture: Fujitsu264Protocol's
            /// FanAngle field is documented as 1-7, but this unit has 8 positions
            /// (raw[28]'s low nibble = 0x8 for the lowest one). Sending this stops
            /// continuous vertical swing, matching the real remote's behavior.
            void set_vertical_angle(const uint8_t level);
            uint8_t get_vertical_angle() const { return this->vertical_angle_; }

            /// Fixed horizontal (left/right) louver position, 1 (left) .. 5 (right).
            /// Has no field in Fujitsu264Protocol at all; reverse-engineered from a
            /// real-remote capture as raw[10] bit 5 (enable) and raw[28]'s high
            /// nibble (position). Sending this stops continuous horizontal swing.
            /// UNVERIFIED on real hardware beyond the byte-level capture.
            void set_horizontal_angle(const uint8_t level);
            uint8_t get_horizontal_angle() const { return this->horizontal_angle_; }

            /// Temperature adjustment used in auto (heat/cool) mode, -2.0..+2.0 in
            /// 0.5 steps. In auto mode the AC picks the base temperature itself, so
            /// the climate entity's absolute target temperature is meaningless and
            /// pinned to 24; this offset is the only effective control.
            void set_temp_auto_offset(const float offset);
            float get_temp_auto_offset() const { return this->temp_auto_offset_; }

            /// Sync state from a frame captured by ESPHome's built-in AEHA decoder
            /// (e.g. the real remote was used). raw_address/raw_data are bit-reversed
            /// by that decoder relative to the wire encoding; this un-reverses them
            /// internally. Returns true if state was updated.
            bool update_from_aeha(const uint16_t raw_address, const std::vector<uint8_t> &raw_data);
            bool get_weak_dry() const { return this->weak_dry_; }
            bool get_clean() const { return this->ac_.getClean(); }

        protected:
            void control(const climate::ClimateCall &call) override;
            void transmit_state() override;

        private:
            void send();
            void apply_state();

            IRFujitsuAC264 ac_ = IRFujitsuAC264(255); // pin is not used
            bool weak_dry_ = false;
            float temp_auto_offset_ = 0;

            // Climate mode as of the last apply_state()/update_from_aeha(), used
            // by apply_state() to tell a power-on / mode change (frame must carry
            // a mode-change Cmd) from a fan-speed/swing-only change (frame must
            // carry CmdFanSpeed/CmdSwing instead, like the real remote does).
            climate::ClimateMode prev_mode_ = climate::CLIMATE_MODE_OFF;

            // Horizontal swing enable, mirroring the library's own _.Swing
            // (vertical) flag: the library has no concept of this axis at all, so
            // we track and poke raw[10] bit 5 ourselves (see .cpp for details).
            bool horizontal_swing_ = false;

            // Last fixed louver positions, remembered so that turning swing off
            // (either axis) re-sends a concrete position instead of a vague "stay".
            uint8_t vertical_angle_ = 1;
            uint8_t horizontal_angle_ = 1;

            // Set by apply_state() when this cycle turns horizontal swing off with
            // a fixed angle: send() must patch Cmd/raw[28] and recompute the
            // checksum itself, since getRaw()'s checkSum() unconditionally forces
            // raw[28]'s high nibble to 0xF (it assumes that nibble is unused).
            bool pending_horizontal_angle_ = false;
        };
    } // namespace fujitsu_264
} // namespace esphome
