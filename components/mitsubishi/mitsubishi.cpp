#include <algorithm>
#include <cstring>

#include "esphome/core/helpers.h"

#include "mitsubishi.h"

namespace esphome
{
    namespace mitsubishi
    {
        // copied from ir_Mitsubishi.cpp
        // Mitsubishi A/C
        const uint16_t kMitsubishiAcHdrMark = 3400;
        const uint16_t kMitsubishiAcHdrSpace = 1750;
        const uint16_t kMitsubishiAcBitMark = 450;
        const uint16_t kMitsubishiAcOneSpace = 1300;
        const uint16_t kMitsubishiAcZeroSpace = 420;
        const uint16_t kMitsubishiAcRptMark = 440;
        const uint16_t kMitsubishiAcRptSpace = 15500;

        // Mitsubishi 136 bit A/C
        const uint16_t kMitsubishi136HdrMark = 3324;
        const uint16_t kMitsubishi136HdrSpace = 1474;
        const uint16_t kMitsubishi136BitMark = 467;
        const uint16_t kMitsubishi136OneSpace = 1137;
        const uint16_t kMitsubishi136ZeroSpace = 351;
        const uint32_t kMitsubishi136Gap = kDefaultMessageGap;

        // Mitsubishi 112 bit A/C
        const uint16_t kMitsubishi112HdrMark = 3450;
        const uint16_t kMitsubishi112HdrSpace = 1696;
        const uint16_t kMitsubishi112BitMark = 450;
        const uint16_t kMitsubishi112OneSpace = 1250;
        const uint16_t kMitsubishi112ZeroSpace = 385;
        const uint32_t kMitsubishi112Gap = kDefaultMessageGap;

        static const char *const TAG = "mitsubishi.climate";

        // AEHA-family address the remote uses for its short toggle codes (dry
        // level cycle / internal clean button), separate from the main
        // MITSUBISHI_AC state frame. Reverse-engineered from a real-remote
        // capture; unlike fujitsu_264 this does NOT need bit-reversal (confirmed
        // by the pre-existing working `aeha:` binary_sensor matcher this
        // replaces).
        static const uint16_t kMitsubishiAcCleanToggleAddress = 0xC4D3;
        // Byte 14 bit 2 of the MITSUBISHI_AC state frame: unused by the library's
        // own Mitsubishi144Protocol bitfield layout (only bit 5, Ecocool, is
        // modeled there), reverse-engineered from a real-remote "internal clean"
        // capture. UNVERIFIED on real hardware beyond the byte-level capture.
        static const uint8_t kMitsubishiAcCleanBit = 0x04;

        // Byte 15 bit 4: a gap between the library's own DirectIndirect (bits
        // 0-1), AbsenseDetect (bit 2) and iSave10C (bit 5) fields in that
        // byte. Confirmed via two real captures (Powerful ON vs OFF) that
        // differed in only this bit.
        static const uint8_t kMitsubishiAcPowerfulBit = 0x10;

        // Byte 6 bit 2: the mode byte's low 3 bits are unused by the library's
        // own bitfield layout (Mode is bits 3-5, ISee bit 6). Confirmed via
        // real captures of the remote's 電流切換 (current limit) button: the
        // Heat-mode 通常->小 press changed only this bit (byte 6 0x08 -> 0x0C,
        // plus checksum), and the same bit tracked the setting in Cool
        // (0x18 -> 0x1C). The remote's short AEHA side-channel frame mirrors
        // it too (data[4] bit 5), but the main state frame always accompanies
        // it, so only this bit is used for both send and receive sync.
        static const uint8_t kMitsubishiAcCurrentCutBit = 0x04;

        // Dry-mode strength, encoded in byte 8's low nibble (the high nibble is
        // WideVane, per the library's own layout -- the low nibble is otherwise
        // unused). Reverse-engineered from a real-remote capture.
        static uint8_t dry_level_to_nibble(const uint8_t level)
        {
            switch (level)
            {
            case 0:
                return 0x4; // weak / "弱"
            case 2:
                return 0x0; // strong / "強"
            default:
                return 0x2; // normal / "標準"
            }
        }

        static uint8_t nibble_to_dry_level(const uint8_t nibble)
        {
            switch (nibble)
            {
            case 0x4:
                return 0;
            case 0x0:
                return 2;
            default:
                return 1;
            }
        }

        climate::ClimateTraits MitsubishiClimate::traits()
        {
            auto traits = climate_ir::ClimateIR::traits();

            auto modes = traits.get_supported_modes();
            if (this->supports_auto_override_.has_value() && !*this->supports_auto_override_)
                modes.erase(climate::CLIMATE_MODE_HEAT_COOL);
            if (this->supports_fan_only_override_.has_value() && !*this->supports_fan_only_override_)
                modes.erase(climate::CLIMATE_MODE_FAN_ONLY);
            traits.set_supported_modes(modes);

            if (this->horizontal_swing_override_.has_value() && !*this->horizontal_swing_override_)
            {
                auto swings = traits.get_supported_swing_modes();
                swings.erase(climate::CLIMATE_SWING_HORIZONTAL);
                swings.erase(climate::CLIMATE_SWING_BOTH);
                traits.set_supported_swing_modes(swings);
            }

            if (this->supports_quiet_fan_override_.has_value() && !*this->supports_quiet_fan_override_)
            {
                auto fan_modes = traits.get_supported_fan_modes();
                fan_modes.erase(climate::CLIMATE_FAN_QUIET);
                traits.set_supported_fan_modes(fan_modes);
            }

            return traits;
        }

        void MitsubishiClimate::control(const climate::ClimateCall &call)
        {
            if (call.get_swing_mode().has_value())
            {
                const bool was_vertical = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL ||
                                            this->swing_mode == climate::CLIMATE_SWING_BOTH);
                const auto new_swing = *call.get_swing_mode();
                const bool will_be_vertical = (new_swing == climate::CLIMATE_SWING_VERTICAL ||
                                                new_swing == climate::CLIMATE_SWING_BOTH);
                // Turning vertical swing off via the climate's own swing
                // control (as opposed to set_vertical_vane() picking a fixed
                // position, which manages swing_mode itself) resets the
                // remembered vane position back to auto, rather than silently
                // keeping whatever fixed position was last selected.
                if (was_vertical && !will_be_vertical)
                    this->vertical_vane_ = kMitsubishiAcVaneAuto;
            }
            climate_ir::ClimateIR::control(call);
        }

        void MitsubishiClimate::set_model(const Model model)
        {
            this->model_ = model;

            switch (this->model_)
            {
            case Model::MITSUBISHI_AC:
                this->minimum_temperature_ = kMitsubishiAcMinTemp;
                this->maximum_temperature_ = kMitsubishiAcMaxTemp;
                this->supports_fan_only_ = true;
                this->fan_modes_ = {
                    climate::CLIMATE_FAN_AUTO,
                    climate::CLIMATE_FAN_LOW,
                    climate::CLIMATE_FAN_MEDIUM,
                    climate::CLIMATE_FAN_HIGH,
                    climate::CLIMATE_FAN_QUIET,
                };
                this->swing_modes_ = {
                    climate::CLIMATE_SWING_OFF,
                    climate::CLIMATE_SWING_VERTICAL,
                    climate::CLIMATE_SWING_HORIZONTAL,
                    climate::CLIMATE_SWING_BOTH
                };
                break;
            case Model::MITSUBISHI136:
                this->minimum_temperature_ = kMitsubishi136MinTemp;
                this->maximum_temperature_ = kMitsubishi136MaxTemp;
                this->supports_fan_only_ = true;
                this->fan_modes_ = {
                    climate::CLIMATE_FAN_LOW,
                    climate::CLIMATE_FAN_MEDIUM,
                    climate::CLIMATE_FAN_HIGH,
                    climate::CLIMATE_FAN_QUIET
                };
                this->swing_modes_ = {
                    climate::CLIMATE_SWING_OFF,
                    climate::CLIMATE_SWING_VERTICAL
                };
                break;
            case Model::MITSUBISHI112:
                this->minimum_temperature_ = kMitsubishi112MinTemp;
                this->maximum_temperature_ = kMitsubishi112MaxTemp;
                this->supports_fan_only_ = false;
                this->fan_modes_ = {
                    climate::CLIMATE_FAN_LOW,
                    climate::CLIMATE_FAN_MEDIUM,
                    climate::CLIMATE_FAN_HIGH,
                    climate::CLIMATE_FAN_QUIET,
                };
                this->swing_modes_ = {
                    climate::CLIMATE_SWING_OFF,
                    climate::CLIMATE_SWING_VERTICAL,
                    climate::CLIMATE_SWING_HORIZONTAL,
                    climate::CLIMATE_SWING_BOTH
                };
                break;
            }
        }

        void MitsubishiClimate::setup()
        {
            climate_ir::ClimateIR::setup();
            this->apply_state();
        }

        void MitsubishiClimate::transmit_state()
        {
            this->apply_state();
            this->send();
        }

        void MitsubishiClimate::send()
        {
            uint8_t *message;

            switch (this->model_)
            {
            case Model::MITSUBISHI_AC:
                message = this->ac_.getRaw();
                // getRaw() above already recomputed the checksum for the fields
                // the library knows about; poke our own custom bits (unused by
                // the library's own bitfield layout) and redo it ourselves so
                // they're covered too. Mirrors fujitsu_264's pending_horizontal_angle_
                // pattern in its own send().
                message[8] = (message[8] & 0xF0) | dry_level_to_nibble(this->dry_level_);
                if (this->clean_)
                    message[14] |= kMitsubishiAcCleanBit;
                else
                    message[14] &= ~kMitsubishiAcCleanBit;
                if (this->powerful_)
                    message[15] |= kMitsubishiAcPowerfulBit;
                else
                    message[15] &= ~kMitsubishiAcPowerfulBit;
                if (this->current_cut_)
                    message[6] |= kMitsubishiAcCurrentCutBit;
                else
                    message[6] &= ~kMitsubishiAcCurrentCutBit;
                {
                    uint8_t sum = 0;
                    for (uint8_t i = 0; i < kMitsubishiACStateLength - 1; i++)
                        sum += message[i];
                    message[kMitsubishiACStateLength - 1] = sum;
                }
                sendGeneric(
                    kMitsubishiAcHdrMark, kMitsubishiAcHdrSpace,
                    kMitsubishiAcBitMark, kMitsubishiAcOneSpace,
                    kMitsubishiAcBitMark, kMitsubishiAcZeroSpace,
                    kMitsubishiAcRptMark, kMitsubishiAcRptSpace,
                    message, kMitsubishiACStateLength,
                    38000
                );
                break;
            case Model::MITSUBISHI136:
                message = this->ac_136_.getRaw();
                sendGeneric(
                    kMitsubishi136HdrMark, kMitsubishi136HdrSpace,
                    kMitsubishi136BitMark, kMitsubishi136OneSpace,
                    kMitsubishi136BitMark, kMitsubishi136ZeroSpace,
                    kMitsubishi136BitMark, kMitsubishi136Gap,
                    message, kMitsubishi136StateLength,
                    38000
                );
                break;
            case Model::MITSUBISHI112:
            default:
                message = this->ac_112_.getRaw();
                sendGeneric(
                    kMitsubishi112HdrMark, kMitsubishi112HdrSpace,
                    kMitsubishi112BitMark, kMitsubishi112OneSpace,
                    kMitsubishi112BitMark, kMitsubishi112ZeroSpace,
                    kMitsubishi112BitMark, kMitsubishi112Gap,
                    message, kMitsubishi112StateLength,
                    38000
                );
                break;
            }
        }

        void MitsubishiClimate::apply_state()
        {
            switch (this->model_)
            {
            case Model::MITSUBISHI_AC:
                apply_state_ac();
                break;
            case Model::MITSUBISHI136:
                apply_state_136();
                break;
            case Model::MITSUBISHI112:
                apply_state_112();
                break;
            }
        }

        void MitsubishiClimate::apply_state_ac()
        {
            if (this->mode == climate::CLIMATE_MODE_OFF)
            {
                this->ac_.off();
            }
            else
            {
                this->ac_.setTemp(this->target_temperature);

                switch (this->mode)
                {
                case climate::CLIMATE_MODE_HEAT_COOL:
                    this->ac_.setMode(kMitsubishiAcAuto);
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    this->ac_.setMode(kMitsubishiAcHeat);
                    break;
                case climate::CLIMATE_MODE_COOL:
                    this->ac_.setMode(kMitsubishiAcCool);
                    break;
                case climate::CLIMATE_MODE_DRY:
                    this->ac_.setMode(kMitsubishiAcDry);
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    this->ac_.setMode(kMitsubishiAcFan);
                    break;
                }

                if (this->fan_mode.has_value())
                {
                    // This unit's own remote uses raw fan speeds 1/2/3 directly
                    // for its three labeled low/medium/high buttons (confirmed
                    // against a real-remote capture: update_from_raw() decoded
                    // them as exactly 1/2/3, not the every-other-value scheme
                    // some other units in this family use). 0 is auto, 6 is
                    // silent (stored internally as 5 by setFan(), see getFan()'s
                    // own quirks noted in update_from_raw() below).
                    switch (this->fan_mode.value())
                    {
                    case climate::CLIMATE_FAN_AUTO:
                        this->ac_.setFan(kMitsubishiAcFanAuto);
                        break;
                    case climate::CLIMATE_FAN_QUIET:
                        this->ac_.setFan(kMitsubishiAcFanSilent);
                        break;
                    case climate::CLIMATE_FAN_LOW:
                        this->ac_.setFan(1);
                        break;
                    case climate::CLIMATE_FAN_MEDIUM:
                        this->ac_.setFan(2);
                        break;
                    case climate::CLIMATE_FAN_HIGH:
                        this->ac_.setFan(3);
                        break;
                    }
                }

                switch (this->swing_mode)
                {
                case climate::CLIMATE_SWING_OFF:
                    // Fixed position remembered from set_vertical_vane() (auto
                    // by default), not always Middle -- lets a user-selected
                    // vane position survive independently of swing on/off.
                    this->ac_.setVane(this->vertical_vane_);
                    this->ac_.setWideVane(kMitsubishiAcWideVaneMiddle);
                    break;
                case climate::CLIMATE_SWING_VERTICAL:
                    this->ac_.setVane(kMitsubishiAcVaneSwing);
                    this->ac_.setWideVane(kMitsubishiAcWideVaneMiddle);
                    break;
                case climate::CLIMATE_SWING_HORIZONTAL:
                    this->ac_.setVane(this->vertical_vane_);
                    this->ac_.setWideVane(kMitsubishiAcWideVaneAuto);
                    break;
                case climate::CLIMATE_SWING_BOTH:
                    this->ac_.setVane(kMitsubishiAcVaneSwing);
                    this->ac_.setWideVane(kMitsubishiAcWideVaneAuto);
                    break;
                }

                this->ac_.on();
            }

            ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
        }

        void MitsubishiClimate::apply_state_136()
        {
            if (this->mode == climate::CLIMATE_MODE_OFF)
            {
                this->ac_136_.off();
            }
            else
            {
                this->ac_136_.setTemp(this->target_temperature);

                switch (this->mode)
                {
                case climate::CLIMATE_MODE_HEAT_COOL:
                    this->ac_136_.setMode(kMitsubishi136Auto);
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    this->ac_136_.setMode(kMitsubishi136Heat);
                    break;
                case climate::CLIMATE_MODE_COOL:
                    this->ac_136_.setMode(kMitsubishi136Cool);
                    break;
                case climate::CLIMATE_MODE_DRY:
                    this->ac_136_.setMode(kMitsubishi136Dry);
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    this->ac_136_.setMode(kMitsubishi136Fan);
                    break;
                }

                if (this->fan_mode.has_value())
                {
                    switch (this->fan_mode.value())
                    {
                    case climate::CLIMATE_FAN_QUIET:
                        this->ac_136_.setFan(kMitsubishi136FanMin);
                        break;
                    case climate::CLIMATE_FAN_LOW:
                        this->ac_136_.setFan(kMitsubishi136FanLow);
                        break;
                    case climate::CLIMATE_FAN_MEDIUM:
                        this->ac_136_.setFan(kMitsubishi136FanMed);
                        break;
                    case climate::CLIMATE_FAN_HIGH:
                        this->ac_136_.setFan(kMitsubishi136FanMax);
                        break;
                    }
                }

                switch (this->swing_mode)
                {
                case climate::CLIMATE_SWING_OFF:
                    this->ac_136_.setSwingV(kMitsubishi112SwingVMiddle);
                    break;
                case climate::CLIMATE_SWING_VERTICAL:
                    this->ac_136_.setSwingV(kMitsubishi136SwingVAuto);
                    break;
                }

                this->ac_136_.on();
            }

            ESP_LOGI(TAG, "%s", this->ac_136_.toString().c_str());
        }

        void MitsubishiClimate::apply_state_112()
        {
            if (this->mode == climate::CLIMATE_MODE_OFF)
            {
                this->ac_112_.off();
            }
            else
            {
                this->ac_112_.setTemp(this->target_temperature);

                switch (this->mode)
                {
                case climate::CLIMATE_MODE_HEAT_COOL:
                    this->ac_112_.setMode(kMitsubishi112Auto);
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    this->ac_112_.setMode(kMitsubishi112Heat);
                    break;
                case climate::CLIMATE_MODE_COOL:
                    this->ac_112_.setMode(kMitsubishi112Cool);
                    break;
                case climate::CLIMATE_MODE_DRY:
                    this->ac_112_.setMode(kMitsubishi112Dry);
                    break;
                }

                if (this->fan_mode.has_value())
                {
                    switch (this->fan_mode.value())
                    {
                    case climate::CLIMATE_FAN_QUIET:
                        this->ac_112_.setFan(kMitsubishi112FanMin);
                        break;
                    case climate::CLIMATE_FAN_LOW:
                        this->ac_112_.setFan(kMitsubishi112FanLow);
                        break;
                    case climate::CLIMATE_FAN_MEDIUM:
                        this->ac_112_.setFan(kMitsubishi112FanMed);
                        break;
                    case climate::CLIMATE_FAN_HIGH:
                        this->ac_112_.setFan(kMitsubishi112FanMax);
                        break;
                    }
                }

                switch (this->swing_mode)
                {
                case climate::CLIMATE_SWING_OFF:
                    this->ac_112_.setSwingV(kMitsubishi112SwingVMiddle);
                    this->ac_112_.setSwingH(kMitsubishi112SwingHMiddle);
                    break;
                case climate::CLIMATE_SWING_VERTICAL:
                    this->ac_112_.setSwingV(kMitsubishi112SwingVAuto);
                    this->ac_112_.setSwingH(kMitsubishi112SwingHMiddle);
                    break;
                case climate::CLIMATE_SWING_HORIZONTAL:
                    this->ac_112_.setSwingV(kMitsubishi112SwingVMiddle);
                    this->ac_112_.setSwingH(kMitsubishi112SwingHAuto);
                    break;
                case climate::CLIMATE_SWING_BOTH:
                    this->ac_112_.setSwingV(kMitsubishi112SwingVAuto);
                    this->ac_112_.setSwingH(kMitsubishi112SwingHAuto);
                    break;
                }

                this->ac_112_.on();
            }

            ESP_LOGI(TAG, "%s", this->ac_112_.toString().c_str());
        }

        void MitsubishiClimate::set_isee(const bool isee)
        {
            // No no-op guard: unlike set_clean()/set_dry_level(), ISee isn't
            // (yet) read back by update_from_raw(), so there's no receive-sync
            // feedback loop to break here.
            this->ac_.setISee(isee);
            this->send();
        }

        void MitsubishiClimate::set_clean(const bool clean)
        {
            // No-op when unchanged. Breaks the feedback loop where our own
            // transmission is picked up by the IR receiver, synced back into
            // this state via update_from_raw()/update_from_aeha(), and would
            // otherwise be re-transmitted forever (same reason as fujitsu_264's
            // set_weak_dry()).
            if (this->clean_ == clean)
                return;
            this->clean_ = clean;
            ESP_LOGI(TAG, "Set clean mode to %s", clean ? "ON" : "OFF");
            // Deliberately no send() here: confirmed on real hardware that the
            // flag just needs to ride along on whatever frame is next
            // transmitted for another reason (e.g. turning the unit off) --
            // send() already bakes in the current clean_ value regardless of
            // why it's called, and toggling this on its own shouldn't cause
            // its own IR transmission.
        }

        void MitsubishiClimate::set_dry_level(const uint8_t level)
        {
            const uint8_t clamped = std::min<uint8_t>(level, 2);
            // No-op guard: value-only, same tradeoff as set_vertical_vane()'s
            // guard (see its comment) -- re-selecting an already-stored value
            // from outside dry mode won't itself switch into dry mode; pick a
            // different value, or use the mode control, for that. Required,
            // not just an accepted limitation: select::publish_state() always
            // re-fires on_value, and the sync handlers republish this select
            // on every successful update_from_raw()/update_from_aeha() --
            // including ones where the real remote changed to a *different*
            // mode entirely. A mode-aware guard let that echo fall through
            // and force the just-synced mode back to DRY.
            const bool already_dry = (this->mode == climate::CLIMATE_MODE_DRY);
            if (this->dry_level_ == clamped)
                return;
            this->dry_level_ = clamped;
            ESP_LOGI(TAG, "Set dry level to %d", clamped);
            if (!already_dry)
            {
                this->mode = climate::CLIMATE_MODE_DRY;
                this->publish_state();
            }
            this->transmit_state();
        }

        void MitsubishiClimate::set_vertical_vane(const uint8_t position)
        {
            // kMitsubishiAcVaneAuto/Highest/High/Middle/Low/Lowest are exactly
            // 0..5 in that order, so the clamped position doubles as the raw
            // library value directly.
            const uint8_t clamped = std::min<uint8_t>(position, kMitsubishiAcVaneLowest);
            // No-op guard: value-only, matching fujitsu_264's
            // set_vertical_angle() exactly. This is required (not just an
            // optimization): select::publish_state() -- unlike switch's --
            // always re-fires on_value, so the on_state trigger that keeps
            // "上下角度" in sync after any climate change (including turning
            // swing on) echoes straight back into this method with the
            // unchanged value. A stricter guard that also allowed re-applying
            // a same-value pick while swinging (to force a stop) would let
            // that echo fall through and immediately cancel swing right after
            // it was turned on. Accepted tradeoff (same as fujitsu_264):
            // re-selecting an already-remembered position while swing is on
            // does NOT stop it -- use the swing control for that instead.
            if (this->vertical_vane_ == clamped)
                return;
            this->vertical_vane_ = clamped;

            // Selecting a position (including auto) stops continuous vertical
            // swing, matching fujitsu_264's set_vertical_angle().
            if (this->swing_mode == climate::CLIMATE_SWING_VERTICAL)
                this->swing_mode = climate::CLIMATE_SWING_OFF;
            else if (this->swing_mode == climate::CLIMATE_SWING_BOTH)
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;

            ESP_LOGI(TAG, "Set vertical vane to %d", clamped);
            this->publish_state();
            this->transmit_state();
        }

        void MitsubishiClimate::set_powerful(const bool powerful)
        {
            // No-op guard: same reason as set_dry_level()/set_vertical_vane()
            // above (breaks the receive-sync feedback loop).
            if (this->powerful_ == powerful)
                return;
            this->powerful_ = powerful;
            ESP_LOGI(TAG, "Set powerful mode to %s", powerful ? "ON" : "OFF");
            this->transmit_state();
        }

        void MitsubishiClimate::set_current_cut(const bool current_cut)
        {
            // No-op guard: same reason as set_dry_level()/set_vertical_vane()
            // above (breaks the receive-sync feedback loop).
            if (this->current_cut_ == current_cut)
                return;
            this->current_cut_ = current_cut;
            ESP_LOGI(TAG, "Set current cut to %s", current_cut ? "ON (小)" : "OFF (通常)");
            this->transmit_state();
        }

        bool MitsubishiClimate::update_from_raw(const std::vector<int32_t> &pulses)
        {
            // Receive sync is only implemented for the 18-byte MITSUBISHI_AC
            // model, matching the one real unit this was verified against.
            if (this->model_ != Model::MITSUBISHI_AC)
                return false;

            if (this->rx_locked_out())
            {
                ESP_LOGD(TAG, "Ignoring raw frame received shortly after our own transmission");
                return false;
            }

            std::vector<uint8_t> raw;
            if (!decode_pulses(pulses,
                                kMitsubishiAcHdrMark, kMitsubishiAcHdrSpace,
                                kMitsubishiAcBitMark, kMitsubishiAcOneSpace, kMitsubishiAcZeroSpace,
                                raw))
            {
                ESP_LOGD(TAG, "Raw frame (%u pulses) did not match MITSUBISHI_AC timing", pulses.size());
                return false;
            }

            // Full dump of every frame matching this timing family, including
            // ones with an unexpected length or invalid checksum -- capture
            // tool for mapping unknown buttons (e.g. Powerful), same as
            // update_from_aeha()'s own dump below.
            ESP_LOGD(TAG, "Raw frame (%u bytes): %s", raw.size(),
                     format_hex_pretty(raw.data(), raw.size()).c_str());

            if (raw.size() != kMitsubishiACStateLength)
                return false;

            if (!IRMitsubishiAC::validChecksum(raw.data()))
            {
                ESP_LOGW(TAG, "Ignoring raw frame with invalid checksum");
                return false;
            }

            this->ac_.setRaw(raw.data());

            if (!this->ac_.getPower())
            {
                this->mode = climate::CLIMATE_MODE_OFF;
            }
            else
            {
                switch (this->ac_.getMode())
                {
                case kMitsubishiAcAuto:
                    this->mode = climate::CLIMATE_MODE_HEAT_COOL;
                    break;
                case kMitsubishiAcCool:
                    this->mode = climate::CLIMATE_MODE_COOL;
                    break;
                case kMitsubishiAcHeat:
                    this->mode = climate::CLIMATE_MODE_HEAT;
                    break;
                case kMitsubishiAcDry:
                    this->mode = climate::CLIMATE_MODE_DRY;
                    break;
                case kMitsubishiAcFan:
                    this->mode = climate::CLIMATE_MODE_FAN_ONLY;
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown mode in received frame: %d", this->ac_.getMode());
                    return false;
                }

                this->target_temperature = this->ac_.getTemp();

                // This unit's remote uses raw fan speeds 1/2/3 directly for its
                // three labeled low/medium/high buttons (confirmed against a
                // real-remote capture), matching what apply_state_ac() now
                // sends. getFan() returns kMitsubishiAcFanSilent(6) instead of
                // kMitsubishiAcFanMax(5) for "silent", since setFan() stores it
                // decremented and getFan() maps that stored value back up --
                // see IRMitsubishiAC::setFan()/getFan().
                switch (this->ac_.getFan())
                {
                case kMitsubishiAcFanAuto:
                    this->fan_mode = climate::CLIMATE_FAN_AUTO;
                    break;
                case 1:
                    this->fan_mode = climate::CLIMATE_FAN_LOW;
                    break;
                case 2:
                    this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
                    break;
                case 3:
                    this->fan_mode = climate::CLIMATE_FAN_HIGH;
                    break;
                case kMitsubishiAcFanSilent:
                    this->fan_mode = climate::CLIMATE_FAN_QUIET;
                    break;
                default:
                    this->fan_mode = climate::CLIMATE_FAN_AUTO;
                    break;
                }

                const uint8_t vane = this->ac_.getVane();
                const bool vertical_on = (vane == kMitsubishiAcVaneSwing);
                const bool horizontal_on = (this->ac_.getWideVane() == kMitsubishiAcWideVaneAuto);
                if (vertical_on && horizontal_on)
                    this->swing_mode = climate::CLIMATE_SWING_BOTH;
                else if (vertical_on)
                    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
                else if (horizontal_on)
                    this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
                else
                    this->swing_mode = climate::CLIMATE_SWING_OFF;

                // Only remember an actual fixed position (including auto),
                // not "swing", same reasoning as fujitsu_264's angle sync.
                if (!vertical_on)
                    this->vertical_vane_ = vane;
            }

            // Custom bits the library doesn't model at all (see set_dry_level()/
            // set_clean()); read directly from the decoded frame, not via ac_,
            // since ac_.getRaw() would re-run checksum() first.
            //
            // Byte 8's low nibble only actually carries dry level while in
            // dry mode -- confirmed on real hardware that in other modes it
            // holds some other, mode-correlated value (e.g. reads as "weak"
            // in Cool, "strong" in Heat), not dry level at all. Only sync it
            // while the frame's own mode is DRY, so switching to another
            // mode doesn't clobber the remembered dry level with noise.
            if (this->mode == climate::CLIMATE_MODE_DRY)
                this->dry_level_ = nibble_to_dry_level(raw[8] & 0x0F);
            this->clean_ = (raw[14] & kMitsubishiAcCleanBit) != 0;
            this->powerful_ = (raw[15] & kMitsubishiAcPowerfulBit) != 0;
            this->current_cut_ = (raw[6] & kMitsubishiAcCurrentCutBit) != 0;

            ESP_LOGI(TAG, "Synced state from real remote: %s", this->ac_.toString().c_str());
            this->publish_state();
            return true;
        }

        bool MitsubishiClimate::update_from_aeha(const uint16_t address, const std::vector<uint8_t> &data)
        {
            if (this->rx_locked_out())
            {
                ESP_LOGD(TAG, "Ignoring AEHA frame received shortly after our own transmission");
                return false;
            }

            // Full dump of every AEHA-family frame from this remote, including
            // ones from addresses we don't recognize at all yet -- capture
            // tool for mapping unknown buttons (e.g. Powerful), same as
            // fujitsu_264's own update_from_aeha().
            ESP_LOGD(TAG, "AEHA frame (address 0x%04X, %u bytes): %s", address, data.size(),
                     format_hex_pretty(data.data(), data.size()).c_str());

            if (address != kMitsubishiAcCleanToggleAddress)
                return false;

            if (data.size() < 16)
                return false;

            // Dry level toggle: data[6]/data[7] identify which level the remote
            // just cycled to. Reverse-engineered from a real-remote capture.
            // Confirmed on real hardware that this side-channel frame keeps
            // arriving with the same kind of dry-level-position payload even
            // while in other modes (e.g. Heat) -- same "only means anything
            // in dry mode" restriction as the main state frame's byte 8 low
            // nibble in update_from_raw(), otherwise it clobbers the
            // remembered dry level with noise every time it's rebroadcast.
            if (data[3] == 0x04)
            {
                if (this->mode != climate::CLIMATE_MODE_DRY)
                    return false;

                if (data[6] == 0x2C && data[7] == 0x02)
                    this->dry_level_ = 0; // weak / "弱"
                else if (data[6] == 0x4C && data[7] == 0x01)
                    this->dry_level_ = 1; // normal / "標準"
                else if (data[6] == 0x0C && data[7] == 0x02)
                    this->dry_level_ = 2; // strong / "強"
                else
                    return false;
                this->publish_state();
                return true;
            }

            // Internal clean button press.
            if (data[3] == 0x00 && data[6] == 0x2C)
            {
                this->clean_ = true;
                this->publish_state();
                return true;
            }

            return false;
        }

    } // namespace mitsubishi
} // namespace esphome
