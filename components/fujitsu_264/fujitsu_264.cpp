#include <algorithm>
#include <cstring>

#include "esphome/core/helpers.h"
#include "esphome/components/remote_base/aeha_protocol.h"

#include "fujitsu_264.h"

namespace esphome
{
    namespace fujitsu_264
    {
        // copied from ir_Fujitsu.cpp
        const uint16_t kFujitsuAcHdrMark = 3324;
        const uint16_t kFujitsuAcHdrSpace = 1574;
        const uint16_t kFujitsuAcBitMark = 448;
        const uint16_t kFujitsuAcOneSpace = 1182;
        const uint16_t kFujitsuAcZeroSpace = 390;
        const uint16_t kFujitsuAcMinGap = 8100;

        // Fixed Fujitsu264 manufacturer header, as sent over the wire as the AEHA
        // "address" field (little-endian): raw[0]=0x14, raw[1]=0x63.
        static const uint16_t kFujitsuAc264Address = 0x6314;

        // Undocumented Cmd for setting the horizontal (left/right) louver angle,
        // the sibling of the library's own kFujitsuAc264CmdFanAngle (0x22, which
        // sets the vertical angle). Reverse-engineered from a real-remote capture:
        // "turn off horizontal swing at a fixed position" frames carry this Cmd.
        static const uint8_t kFujitsuAc264CmdFanAngleHoriz = 0x26;
        // raw[10] bit for horizontal swing enable, alongside the library's own
        // Swing bit (bit 4, vertical) in the same byte. Also reverse-engineered.
        static const uint8_t kFujitsuAc264HorizontalSwingBit = 0x20;

        static const char *const TAG = "fujitsu_264.climate";

        // ESPHome's built-in AEHAProtocol decoder reads bits MSB-first, but the
        // Fujitsu264 protocol actually transmits LSB-first (confirmed against a
        // real AR-RLB1J capture: the 5-byte "power off" frame's bit-reversal
        // matched byte-for-byte). So every value it hands us is bit-reversed
        // relative to the wire/raw[] representation and needs to be un-reversed:
        // the 16-bit address as a whole, and each data byte independently.
        // (reverse_bits8/16 now live on IrRemoteBase, shared with every other
        // component on this fork whose receive-sync entry point is on_aeha.)

        void Fujitsu264Climate::setup()
        {
            climate_ir::ClimateIR::setup();
            this->apply_state();
        }

        void Fujitsu264Climate::control(const climate::ClimateCall &call)
        {
            // ClimateIR::control() (below) only reads the standard fan_mode enum;
            // since we expose custom fan modes instead (real-remote labels, not
            // ESPHome's generic Low/Medium/High), it never sees a plain fan_mode
            // call. Apply the custom one ourselves first.
            if (call.has_custom_fan_mode())
                this->set_custom_fan_mode_(call.get_custom_fan_mode());
            climate_ir::ClimateIR::control(call);
        }

        bool Fujitsu264Climate::on_receive(remote_base::RemoteReceiveData data)
        {
            // Entry point for receive-sync: replaces the YAML-side `on_aeha:`
            // trigger that used to call update_from_aeha() by hand. Requires
            // `receiver_id:` to be set on this climate's YAML config, otherwise
            // ClimateIR never registers us as a RemoteReceiverListener at all.
            auto aeha = remote_base::AEHAProtocol().decode(data);
            if (!aeha.has_value())
                return false;
            return this->update_from_aeha(aeha->address, aeha->data);
        }

        void Fujitsu264Climate::set_fan_angle(const uint8_t fan_angle)
        {
            this->ac_.setFanAngle(fan_angle);
            ESP_LOGI(TAG, "Set fan angle to %d", fan_angle);
            this->send();
        }

        void Fujitsu264Climate::set_vertical_angle(const uint8_t level)
        {
            const uint8_t clamped = std::min<uint8_t>(std::max<uint8_t>(level, 1), 8);
            // No-op when unchanged. This also breaks the feedback loop where our
            // own transmission is picked up by the IR receiver, synced back into
            // this select via update_from_aeha(), and would otherwise be
            // re-transmitted forever (same reason as set_weak_dry()). Note this
            // means re-selecting the same already-remembered angle while swing is
            // still on won't force a stop -- switch the climate's swing control
            // to OFF for that instead.
            if (this->vertical_angle_ == clamped)
                return;
            this->vertical_angle_ = clamped;

            // Picking an explicit angle stops continuous vertical swing (matches a
            // real-remote capture: fixed-angle frames have the Swing bit clear).
            // Call setSwing() first so its Cmd/FanAngle=Stay get overwritten by
            // setFanAngle() below, not the other way around.
            this->ac_.setSwing(false);
            // The library's setFanAngle() only accepts 1-7 (clamps 8 to "Stay"),
            // but this unit has 8 vertical positions (confirmed: a real "lowest
            // position" capture has raw[28]'s low nibble = 0x8). Route around the
            // clamp for level 8 by writing the nibble directly; checkSum() never
            // touches the low nibble of raw[28], so this survives to send().
            this->ac_.setFanAngle(std::min<uint8_t>(clamped, 7));
            if (clamped == 8)
            {
                uint8_t *raw = this->ac_.getRaw();
                raw[28] = (raw[28] & 0xF0) | 0x08;
            }

            if (this->swing_mode == climate::CLIMATE_SWING_VERTICAL)
                this->swing_mode = climate::CLIMATE_SWING_OFF;
            else if (this->swing_mode == climate::CLIMATE_SWING_BOTH)
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;

            ESP_LOGI(TAG, "Set vertical angle to %d", clamped);
            this->publish_state();
            this->send();
        }

        void Fujitsu264Climate::set_horizontal_angle(const uint8_t level)
        {
            const uint8_t clamped = std::min<uint8_t>(std::max<uint8_t>(level, 1), 5);
            // No-op guard: same reason as set_vertical_angle() above.
            if (this->horizontal_angle_ == clamped)
                return;
            this->horizontal_angle_ = clamped;
            this->horizontal_swing_ = false;

            if (this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL)
                this->swing_mode = climate::CLIMATE_SWING_OFF;
            else if (this->swing_mode == climate::CLIMATE_SWING_BOTH)
                this->swing_mode = climate::CLIMATE_SWING_VERTICAL;

            // No native library support for horizontal angle at all -- raw[10]
            // bit 5 and raw[28]'s high nibble were reverse-engineered from a
            // real-remote capture. getRaw() also primes the fixed/checksum bytes;
            // send() will patch Cmd/raw[28]/checksum for the actual angle value
            // (see pending_horizontal_angle_ there for why it can't be done here).
            uint8_t *raw = this->ac_.getRaw();
            raw[10] &= ~kFujitsuAc264HorizontalSwingBit;
            this->pending_horizontal_angle_ = true;

            ESP_LOGI(TAG, "Set horizontal angle to %d", clamped);
            this->publish_state();
            this->send();
        }

        void Fujitsu264Climate::toggle_powerful()
        {
            // Note: IRFujitsuAC264::togglePowerful() silently no-ops unless the
            // library's internal _ispoweredon flag is true, but that flag is only
            // ever set inside IRFujitsuAC264::send(), which we never call (we
            // transmit via ESPHome's sendGeneric() instead). So it's always false
            // and the toggle command never actually gets sent. Work around this by
            // checking the climate's own (accurate) power state and writing the
            // toggle-powerful frame directly via the public setRaw() API, which
            // reproduces exactly what togglePowerful() does internally minus the
            // broken guard.
            if (this->mode == climate::CLIMATE_MODE_OFF)
            {
                ESP_LOGW(TAG, "Ignoring powerful mode toggle: AC is off");
                return;
            }
            this->ac_.setRaw(kFujitsuAc264StatesTogglePowerful, kFujitsuAc264StateLengthShort);
            ESP_LOGI(TAG, "Toggled powerful mode");
            this->send();
        }

        void Fujitsu264Climate::set_clean(const bool clean)
        {
            // No-op when unchanged. Also breaks the feedback loop where our own
            // transmission is picked up by the IR receiver, synced back into
            // this state via update_from_aeha(), and would otherwise be
            // re-transmitted forever (same reason as set_weak_dry()).
            if (this->clean_ == clean)
                return;
            this->clean_ = clean;
            ESP_LOGI(TAG, "Set clean mode to %s", clean ? "ON" : "OFF");
            this->send();
        }

        void Fujitsu264Climate::toggle_sterilization()
        {
            this->ac_.toggleSterilization();
            ESP_LOGI(TAG, "Toggled sterilization");
            this->send();
        }

        void Fujitsu264Climate::set_weak_dry(const bool weak_dry)
        {
            // No-op guard: value-only. Re-selecting an already-stored value
            // from outside dry mode won't itself switch into dry mode; pick a
            // different value, or use the mode control, for that (same
            // tradeoff as set_vertical_angle()'s guard). Required, not just
            // an accepted limitation: the select's on_value always re-fires
            // when update_from_aeha() republishes it after every successful
            // sync -- including ones where the real remote changed to a
            // *different* mode entirely. A mode-aware guard
            // (weak_dry_==weak_dry && already_dry) let that echo fall
            // through and force the just-synced mode back to DRY.
            const bool already_dry = (this->mode == climate::CLIMATE_MODE_DRY);
            if (this->weak_dry_ == weak_dry)
                return;
            this->weak_dry_ = weak_dry;
            ESP_LOGI(TAG, "Set weak dry to %s", weak_dry ? "ON" : "OFF");
            if (!already_dry)
            {
                this->mode = climate::CLIMATE_MODE_DRY;
                this->publish_state();
            }
            this->transmit_state();
        }

        void Fujitsu264Climate::set_temp_auto_offset(const float offset)
        {
            // No-op when unchanged, for the same feedback-loop reason as
            // set_weak_dry(): received frames are synced back into the HA number
            // entity, whose on_value handler calls this again.
            if (this->temp_auto_offset_ == offset)
                return;
            this->temp_auto_offset_ = offset;
            ESP_LOGI(TAG, "Set auto-mode temperature offset to %.1f", offset);
            // retransmit only if the change is relevant now
            if (this->mode == climate::CLIMATE_MODE_HEAT_COOL)
            {
                this->transmit_state();
            }
        }

        void Fujitsu264Climate::transmit_state()
        {
            this->apply_state();
            this->send();
        }

        void Fujitsu264Climate::send()
        {
            // Re-applied on every transmission regardless of what triggered it
            // (transmit_state()'s apply_state()+send(), or a lightweight setter
            // like set_clean() itself that calls send() directly) -- mirrors
            // mitsubishi::send()'s own re-poke of its custom bits, so
            // this->clean_ is always the source of truth for the actual
            // outgoing frame, not just for whatever apply_state() last built.
            this->ac_.setClean(this->clean_);

            uint8_t *message = this->ac_.getRaw();
            uint8_t length = this->ac_.getStateLength();

            if (this->pending_horizontal_angle_)
            {
                // checkSum() (invoked by getRaw() above) unconditionally forces
                // raw[28]'s high nibble to 0xF, assuming it's unused -- but on this
                // unit it's the horizontal angle. Overwrite it and Cmd, then
                // recompute the checksum ourselves to undo that clobber.
                message[18] = kFujitsuAc264CmdFanAngleHoriz;
                message[28] = (message[28] & 0x0F) | (this->horizontal_angle_ << 4);
                uint8_t sum = 0;
                for (uint8_t i = 0; i < length - 1; i++)
                    sum += message[i];
                message[length - 1] = static_cast<uint8_t>(0xAF - sum);
                this->pending_horizontal_angle_ = false;
            }

            // Logged at INFO (not DEBUG) so a fan-speed/swing change can be
            // diffed against a real-remote capture without switching log levels.
            ESP_LOGI(TAG, "Sending frame (%u bytes): %s", length,
                     format_hex_pretty(message, length).c_str());

            sendGeneric(
                kFujitsuAcHdrMark, kFujitsuAcHdrSpace,
                kFujitsuAcBitMark, kFujitsuAcOneSpace,
                kFujitsuAcBitMark, kFujitsuAcZeroSpace,
                kFujitsuAcBitMark, kFujitsuAcMinGap,
                message, length,
                38000
            );
        }

        bool Fujitsu264Climate::update_from_aeha(const uint16_t raw_address, const std::vector<uint8_t> &raw_data)
        {
            const uint16_t address = reverse_bits16(raw_address);
            if (address != kFujitsuAc264Address)
                return false;

            if (this->rx_locked_out())
            {
                ESP_LOGD(TAG, "Ignoring AEHA frame received shortly after our own transmission");
                return false;
            }

            std::vector<uint8_t> raw;
            raw.reserve(2 + raw_data.size());
            raw.push_back(static_cast<uint8_t>(address & 0xFF));
            raw.push_back(static_cast<uint8_t>((address >> 8) & 0xFF));
            for (uint8_t b : raw_data)
                raw.push_back(reverse_bits8(b));

            // Full wire-order dump of every frame from the real remote, including
            // ones we don't understand yet. This is the capture tool for mapping
            // unknown buttons (e.g. horizontal swing, which has no field in the
            // library's Fujitsu264Protocol): press the button, read the bytes here.
            // Cmd is raw[18] on full-length frames.
            ESP_LOGD(TAG, "AEHA frame from remote (%u bytes): %s", raw.size(),
                     format_hex_pretty(raw.data(), raw.size()).c_str());

            // Power-off is a short special frame with no mode/temp/fan payload.
            if (raw.size() == kFujitsuAc264StateLengthShort &&
                std::memcmp(raw.data(), kFujitsuAc264StatesTurnOff, kFujitsuAc264StateLengthShort) == 0)
            {
                ESP_LOGI(TAG, "Synced state from real remote: OFF");
                this->mode = climate::CLIMATE_MODE_OFF;
                this->prev_mode_ = this->mode;
                this->publish_state();
                return true;
            }

            // Anything else that isn't a full state frame (clean/sterilization/
            // powerful/eco-fan toggles, outside-quiet, ...) doesn't carry
            // mode/temp/fan info we can reflect onto the climate entity.
            if (raw.size() != kFujitsuAc264StateLength)
                return false;

            if (!IRFujitsuAC264::validChecksum(raw.data(), raw.size()))
            {
                ESP_LOGW(TAG, "Ignoring AEHA frame with invalid checksum");
                return false;
            }

            if (!this->ac_.setRaw(raw.data(), raw.size()))
                return false;

            switch (this->ac_.getMode())
            {
            case kFujitsuAc264ModeAuto:
                this->mode = climate::CLIMATE_MODE_HEAT_COOL;
                break;
            case kFujitsuAc264ModeCool:
                this->mode = climate::CLIMATE_MODE_COOL;
                break;
            case kFujitsuAc264ModeHeat:
                this->mode = climate::CLIMATE_MODE_HEAT;
                break;
            case kFujitsuAc264ModeDry:
                this->mode = climate::CLIMATE_MODE_DRY;
                break;
            case kFujitsuAc264ModeFan:
                this->mode = climate::CLIMATE_MODE_FAN_ONLY;
                break;
            default:
                ESP_LOGW(TAG, "Unknown mode in received frame: %d", this->ac_.getMode());
                return false;
            }

            switch (this->ac_.getFanSpeed())
            {
            case kFujitsuAc264FanSpeedQuiet:
                this->set_custom_fan_mode_(kFanModeQuiet);
                break;
            case kFujitsuAc264FanSpeedLow:
                this->set_custom_fan_mode_(kFanModeLow);
                break;
            case kFujitsuAc264FanSpeedMed:
                this->set_custom_fan_mode_(kFanModeMedium);
                break;
            case kFujitsuAc264FanSpeedHigh:
                this->set_custom_fan_mode_(kFanModeHigh);
                break;
            default:
                this->set_custom_fan_mode_(kFanModeAuto);
                break;
            }

            if (this->mode == climate::CLIMATE_MODE_HEAT_COOL)
            {
                // Auto mode carries a -2..+2 offset instead of an absolute target.
                this->temp_auto_offset_ = this->ac_.getTempAuto();
                this->target_temperature = 24.0f;
            }
            else
            {
                this->target_temperature = this->ac_.getTemp();
            }
            // Vertical swing/angle come from the library (ac_.setRaw() above
            // already parsed them). Horizontal has no library support at all, so
            // read raw[10] bit 5 and raw[28]'s high nibble directly from the
            // vector we just built -- NOT via ac_.getRaw(), which would run
            // checkSum() and clobber raw[28]'s high nibble before we can read it.
            const bool vertical_on = this->ac_.getSwing();
            const bool horizontal_on = (raw[10] & kFujitsuAc264HorizontalSwingBit) != 0;
            this->horizontal_swing_ = horizontal_on;
            if (vertical_on && horizontal_on)
                this->swing_mode = climate::CLIMATE_SWING_BOTH;
            else if (vertical_on)
                this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
            else if (horizontal_on)
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
            else
                this->swing_mode = climate::CLIMATE_SWING_OFF;

            // 0xF ("stay") isn't a real fixed position, so only remember an
            // actual angle value when one was sent.
            const uint8_t vertical_angle = raw[28] & 0x0F;
            if (vertical_angle != 0x0F)
                this->vertical_angle_ = vertical_angle;
            const uint8_t horizontal_angle = (raw[28] >> 4) & 0x0F;
            if (horizontal_angle != 0x0F)
                this->horizontal_angle_ = horizontal_angle;

            this->weak_dry_ = this->ac_.isWeakDry();
            this->clean_ = this->ac_.getClean();
            this->prev_mode_ = this->mode;

            ESP_LOGI(TAG, "Synced state from real remote: %s", this->ac_.toString().c_str());
            this->publish_state();
            // Replaces the YAML-side manual id(select_x).publish_state(...) calls
            // that used to run after a successful on_aeha: sync. Each child entity
            // is optional (nullptr if not declared in the device's YAML).
            if (this->weak_dry_select_ != nullptr)
                this->weak_dry_select_->publish_state(this->weak_dry_ ? 1 : 0);
            if (this->vertical_angle_select_ != nullptr)
                this->vertical_angle_select_->publish_state(this->vertical_angle_ - 1);
            if (this->horizontal_angle_select_ != nullptr)
                this->horizontal_angle_select_->publish_state(this->horizontal_angle_ - 1);
            if (this->temp_auto_offset_number_ != nullptr)
                this->temp_auto_offset_number_->publish_state(this->temp_auto_offset_);
            if (this->internal_clean_switch_ != nullptr)
                this->internal_clean_switch_->publish_state(this->clean_);
            return true;
        }

        void Fujitsu264Climate::apply_state()
        {
            // Previous state, to detect below which single parameter this
            // transmission is actually changing.
            const bool was_on = this->prev_mode_ != climate::CLIMATE_MODE_OFF;
            const bool mode_changed = this->mode != this->prev_mode_;
            const uint8_t prev_fan_speed = this->ac_.getFanSpeed();
            const bool prev_vertical_swing = this->ac_.getSwing();
            const bool prev_horizontal_swing = this->horizontal_swing_;
            this->prev_mode_ = this->mode;

            if (this->mode == climate::CLIMATE_MODE_OFF)
            {
                this->ac_.off();
            }
            else
            {
                if (this->mode == climate::CLIMATE_MODE_HEAT_COOL)
                {
                    // In auto mode the AC picks the base temperature itself and only
                    // accepts a -2..+2 offset (TempAuto), so the absolute target
                    // temperature is meaningless: pin the display to 24 and send the
                    // offset instead.
                    this->target_temperature = 24.0f;
                    this->ac_.setTempAuto(this->temp_auto_offset_);
                }
                else
                {
                    this->ac_.setTemp(this->target_temperature);
                }

                if (this->has_custom_fan_mode())
                {
                    const auto fan = this->get_custom_fan_mode();
                    if (fan == kFanModeAuto)
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedAuto);
                    else if (fan == kFanModeQuiet)
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedQuiet);
                    else if (fan == kFanModeLow)
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedLow);
                    else if (fan == kFanModeMedium)
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedMed);
                    else if (fan == kFanModeHigh)
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedHigh);
                    else
                        ESP_LOGW(TAG, "Unknown custom fan mode: %s", fan.c_str());
                }

                const bool want_vertical = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL ||
                                            this->swing_mode == climate::CLIMATE_SWING_BOTH);
                const bool want_horizontal = (this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL ||
                                              this->swing_mode == climate::CLIMATE_SWING_BOTH);
                // Vertical swing is confirmed working on the real unit via the
                // library's own setSwing(). Horizontal has no library support at
                // all; poke raw[10] bit 5 directly (reverse-engineered from a
                // real-remote capture) -- UNVERIFIED on real hardware.
                this->ac_.setSwing(want_vertical);
                this->horizontal_swing_ = want_horizontal;
                uint8_t *raw = this->ac_.getRaw();
                if (want_horizontal)
                    raw[10] |= kFujitsuAc264HorizontalSwingBit;
                else
                    raw[10] &= ~kFujitsuAc264HorizontalSwingBit;

                // this->ac_.on() is not needed as it is already handled by the following mode switch
                switch (this->mode)
                {
                case climate::CLIMATE_MODE_HEAT_COOL:
                    this->ac_.setMode(kFujitsuAc264ModeAuto);
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    this->ac_.setMode(kFujitsuAc264ModeHeat);
                    break;
                case climate::CLIMATE_MODE_COOL:
                    this->ac_.setMode(kFujitsuAc264ModeCool);
                    break;
                case climate::CLIMATE_MODE_DRY:
                    this->ac_.setMode(kFujitsuAc264ModeDry, this->weak_dry_);
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    this->ac_.setMode(kFujitsuAc264ModeFan);
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown mode: %d", this->mode);
                    break;
                }

                // The AC applies only the parameter named by the frame's Cmd byte
                // (raw[18]): the real remote sends CmdFanSpeed (0x1E) for a fan
                // change and CmdSwing (0x0B) for a swing change. setMode() above
                // unconditionally stamps a mode-change Cmd (SubCmd=1), which made
                // fan-speed/swing-only changes from HA get ignored by the unit.
                // When neither power nor mode changed, re-stamp the Cmd to name
                // what did change. Temperature is left on the mode Cmd: that path
                // is verified working on the real unit.
                if (was_on && !mode_changed)
                {
                    if (this->ac_.getFanSpeed() != prev_fan_speed)
                        this->ac_.setCmd(kFujitsuAc264CmdFanSpeed);
                    else if (this->ac_.getSwing() != prev_vertical_swing)
                        this->ac_.setCmd(kFujitsuAc264CmdSwing);
                    else if (want_horizontal != prev_horizontal_swing)
                    {
                        if (want_horizontal)
                            // Turning it on: same shared Cmd as vertical, matches
                            // a real-remote "left-right swing ON" capture exactly.
                            this->ac_.setCmd(kFujitsuAc264CmdSwing);
                        else
                            // Turning it off: no capture exists of "off, vague
                            // position" for this axis, only "off, fixed position"
                            // -- so send() will finish this by re-sending the last
                            // remembered horizontal angle via CmdFanAngleHoriz.
                            this->pending_horizontal_angle_ = true;
                    }
                }
            }

            ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
        }

    }  // namespace fujitsu_264
}  // namespace esphome
