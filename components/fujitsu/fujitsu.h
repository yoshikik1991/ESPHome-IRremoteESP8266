#pragma once

#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/optional.h"
#include "esphome/components/ir_remote_base/ir_remote_base.h"
#include "ir_Fujitsu.h"

namespace esphome
{
    namespace fujitsu
    {
        enum Model
        {
            ARRAH2E = fujitsu_ac_remote_model_t::ARRAH2E,
            ARDB1 = fujitsu_ac_remote_model_t::ARDB1,
            ARREB1E = fujitsu_ac_remote_model_t::ARREB1E,
            ARJW2 = fujitsu_ac_remote_model_t::ARJW2,
            ARRY4 = fujitsu_ac_remote_model_t::ARRY4,
            ARREW4E = fujitsu_ac_remote_model_t::ARREW4E
        };

        class FujitsuClimate : public ir_remote_base::IrRemoteBase
        {
        public:
            FujitsuClimate()
                : IrRemoteBase(kFujitsuAcMinTemp, kFujitsuAcMaxTemp, 1.0f, true, true,
                               {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH, climate::CLIMATE_FAN_QUIET},
                               {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

            void set_model(const Model model);

            // Override the model-based detection of horizontal swing support
            // (enabled by default for `ARRAH2E` and `ARJW2` models)
            void set_horizontal_swing_supported(bool supported)
            {
                this->horizontal_swing_override_ = supported;
            }

            void setup() override;
            climate::ClimateTraits traits() override;

            void step_horizontal();
            void step_vertical();

            // Toggle Economy (Eco) mode. Sends a kFujitsuAcCmdEcono command frame.
            // Internally tracks the assumed Eco state, used by the ECO preset.
            // External toggles via the physical IR remote will desync this state
            // until the next ESPHome-initiated mode/temp/fan/swing change.
            void toggle_econo();
            bool is_econo() const { return this->econo_state_; }

            // Toggle Powerful (boost) mode. Same caveats as econo() apply.
            void toggle_powerful();
            bool is_powerful() const { return this->powerful_state_; }

            /// Sync state from a frame captured by ESPHome's built-in AEHA decoder
            /// (e.g. the real remote was used). raw_address/raw_data are bit-reversed
            /// by that decoder relative to the wire encoding; this un-reverses them
            /// internally, mirroring fujitsu_264's own update_from_aeha() (this
            /// legacy protocol shares the same 0x14 0x63 vendor header). Only the
            /// ARRAH2E-family long (16-byte) and short (7-byte) code lengths are
            /// recognized; ARDB1/ARJW2's one-byte-shorter long/short codes are not
            /// handled (no hardware available to confirm the mapping). Short
            /// frames other than power-off (econo/powerful toggles, step-vane
            /// commands) carry no absolute mode/temp/fan/swing payload and are
            /// only logged, not synced onto the climate entity.
            /// UNVERIFIED on real hardware -- compile-tested only. No physical
            /// unit of this protocol family was available; implemented by
            /// mirroring the verified fujitsu_264/mitsubishi receive paths.
            /// Returns true if state was updated.
            bool update_from_aeha(const uint16_t raw_address, const std::vector<uint8_t> &raw_data);

        protected:
            void transmit_state() override;
            void control(const climate::ClimateCall &call) override;
            bool on_receive(remote_base::RemoteReceiveData data) override;

        private:
            void send();
            void apply_state();
            void sync_preset_to_state();

            inline bool supports_horizontal_swing()
            {
                if (this->horizontal_swing_override_.has_value())
                {
                    return this->horizontal_swing_override_.value();
                }
                return this->ac_.getModel() == fujitsu_ac_remote_model_t::ARRAH2E || this->ac_.getModel() == fujitsu_ac_remote_model_t::ARJW2;
            }

            inline bool supports_econo_powerful()
            {
                return this->ac_.getModel() == fujitsu_ac_remote_model_t::ARREB1E || this->ac_.getModel() == fujitsu_ac_remote_model_t::ARREW4E;
            }

            IRFujitsuAC ac_ = IRFujitsuAC(255); // pin is not used

            // Best-effort tracking of toggle-only state bits (Eco / Powerful).
            // Both default to false, matching a fresh power-on of the AC.
            bool econo_state_ = false;
            bool powerful_state_ = false;

            optional<bool> horizontal_swing_override_;
        };

    } // namespace fujitsu
} // namespace esphome
