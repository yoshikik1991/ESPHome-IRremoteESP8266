#include "esphome/core/helpers.h"

#include "sharp.h"

namespace esphome
{
    namespace sharp
    {
        static const char *const TAG = "sharp.climate";

        void SharpClimate::set_model(const Model model)
        {
            this->ac_.setModel((sharp_ac_remote_model_t) model);
        }

        void SharpClimate::setup()
        {
            climate_ir::ClimateIR::setup();
            this->apply_state();
        }

        bool SharpClimate::on_receive(remote_base::RemoteReceiveData data)
        {
            // Entry point for receive-sync. Sharp uses its own leader timing
            // (3800/1900), not AEHA's (3400/1700), so this only ever needs the
            // raw pulse path -- same shape as electra's on_receive(). Requires
            // `receiver_id:` to be set on this climate's YAML config, otherwise
            // ClimateIR never registers us as a RemoteReceiverListener at all.
            data.reset();
            return this->update_from_raw(data.get_raw_data());
        }

        void SharpClimate::transmit_state()
        {
            this->apply_state();
            this->send();
        }

        void SharpClimate::send()
        {
            uint8_t *message = this->ac_.getRaw();

            sendGeneric(
                kSharpAcHdrMark, kSharpAcHdrSpace,
                kSharpAcBitMark, kSharpAcOneSpace,
                kSharpAcBitMark, kSharpAcZeroSpace,
                kSharpAcBitMark, kSharpAcGap,
                message, kSharpAcStateLength,
                38000
            );
        }

        void SharpClimate::apply_state()
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
                case climate::CLIMATE_MODE_AUTO:
                    this->ac_.setMode(kSharpAcAuto);
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    this->ac_.setMode(kSharpAcHeat);
                    break;
                case climate::CLIMATE_MODE_COOL:
                    this->ac_.setMode(kSharpAcCool);
                    break;
                case climate::CLIMATE_MODE_DRY:
                    this->ac_.setMode(kSharpAcDry);
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    this->ac_.setMode(kSharpAcFan);
                    break;
                }

                if (this->fan_mode.has_value())
                {
                    switch (this->fan_mode.value())
                    {
                    case climate::CLIMATE_FAN_AUTO:
                        this->ac_.setFan(kSharpAcFanAuto);
                        break;
                    case climate::CLIMATE_FAN_LOW:
                        this->ac_.setFan(kSharpAcFanMin);
                        break;
                    case climate::CLIMATE_FAN_MEDIUM:
                        this->ac_.setFan(kSharpAcFanMed);
                        break;
                    case climate::CLIMATE_FAN_HIGH:
                        this->ac_.setFan(kSharpAcFanHigh);
                        break;
                    }
                }

                switch (this->swing_mode)
                {
                case climate::CLIMATE_SWING_OFF:
                    this->ac_.setSwingV(kSharpAcSwingVOff);
                    break;
                case climate::CLIMATE_SWING_VERTICAL:
                    this->ac_.setSwingV(kSharpAcSwingVLowest);
                    break;
                }

                this->ac_.on();
            }

            ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
        }

        bool SharpClimate::update_from_raw(const std::vector<int32_t> &pulses)
        {
            if (this->rx_locked_out())
            {
                ESP_LOGD(TAG, "Ignoring raw frame received shortly after our own transmission");
                return false;
            }

            std::vector<uint8_t> raw;
            if (!decode_pulses(pulses,
                                kSharpAcHdrMark, kSharpAcHdrSpace,
                                kSharpAcBitMark, kSharpAcOneSpace, kSharpAcZeroSpace,
                                raw))
            {
                ESP_LOGD(TAG, "Raw frame (%u pulses) did not match Sharp timing", pulses.size());
                return false;
            }

            // Full dump of every frame matching this timing family -- capture
            // tool for mapping unknown buttons, same pattern as mitsubishi's own
            // update_from_raw()/update_from_aeha().
            ESP_LOGD(TAG, "Raw frame (%u bytes): %s", raw.size(),
                     format_hex_pretty(raw.data(), raw.size()).c_str());

            if (raw.size() != kSharpAcStateLength)
                return false;

            if (!IRSharpAc::validChecksum(raw.data(), raw.size()))
            {
                ESP_LOGW(TAG, "Ignoring raw frame with invalid checksum");
                return false;
            }

            // IRSharpAc::setRaw() also re-detects the model from the frame's own
            // Model/Model2 bits (unlike the legacy fujitsu component's coarse
            // Cmd-byte heuristic, this is genuine model info the real remote
            // transmits in every full-state frame, so it's expected/correct to
            // pick it up here -- not the same kind of corruption risk).
            this->ac_.setRaw(raw.data(), raw.size());

            if (!this->ac_.getPower())
            {
                this->mode = climate::CLIMATE_MODE_OFF;
            }
            else
            {
                switch (this->ac_.getMode())
                {
                case kSharpAcHeat:
                    this->mode = climate::CLIMATE_MODE_HEAT;
                    break;
                case kSharpAcCool:
                    this->mode = climate::CLIMATE_MODE_COOL;
                    break;
                case kSharpAcDry:
                    this->mode = climate::CLIMATE_MODE_DRY;
                    break;
                case kSharpAcAuto:
                    // Shares its raw value with kSharpAcFan (both 0b00, so they
                    // can't both be switch cases here): the library disambiguates
                    // by model (kSharpAcAuto is "A907 only"; kSharpAcFan is
                    // "A705 only" -- A705 has no Auto mode and reuses this code
                    // point for Fan instead), mirrored here rather than picking
                    // one arbitrarily.
                    this->mode = (this->ac_.getModel() == sharp_ac_remote_model_t::A705)
                                     ? climate::CLIMATE_MODE_FAN_ONLY
                                     : climate::CLIMATE_MODE_AUTO;
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown mode in received frame: %d", this->ac_.getMode());
                    return false;
                }

                this->target_temperature = this->ac_.getTemp();

                switch (this->ac_.getFan())
                {
                case kSharpAcFanAuto:
                    this->fan_mode = climate::CLIMATE_FAN_AUTO;
                    break;
                case kSharpAcFanMin:
                    this->fan_mode = climate::CLIMATE_FAN_LOW;
                    break;
                case kSharpAcFanMed:
                    this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
                    break;
                case kSharpAcFanHigh:
                    this->fan_mode = climate::CLIMATE_FAN_HIGH;
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown fan speed in received frame: %d", this->ac_.getFan());
                    break;
                }

                // apply_state() only ever sends kSharpAcSwingVOff or
                // kSharpAcSwingVLowest (the 2-value mapping this mirrors); a real
                // remote may report other positions (e.g. kSharpAcSwingVHigh),
                // which all collapse to "on" here since this climate only
                // exposes on/off swing control, not fixed positions.
                this->swing_mode = (this->ac_.getSwingV() == kSharpAcSwingVOff)
                                        ? climate::CLIMATE_SWING_OFF
                                        : climate::CLIMATE_SWING_VERTICAL;
            }

            ESP_LOGI(TAG, "Synced state from real remote: %s", this->ac_.toString().c_str());
            this->publish_state();
            return true;
        }

        void SharpClimate::apply_batch(optional<climate::ClimateMode> mode,
                                        optional<float> target_temperature,
                                        optional<climate::ClimateFanMode> fan_mode,
                                        optional<climate::ClimateSwingMode> swing_mode)
        {
            auto traits = this->traits();
            bool changed = false;

            if (mode.has_value())
            {
                if (traits.supports_mode(*mode))
                {
                    this->mode = *mode;
                    changed = true;
                }
                else
                {
                    ESP_LOGW(TAG, "apply_batch: mode %d not supported by this unit, ignoring", (int)*mode);
                }
            }
            if (target_temperature.has_value())
            {
                this->target_temperature =
                    clamp(*target_temperature, traits.get_visual_min_temperature(), traits.get_visual_max_temperature());
                changed = true;
            }
            if (fan_mode.has_value())
            {
                if (traits.supports_fan_mode(*fan_mode))
                {
                    this->fan_mode = *fan_mode;
                    changed = true;
                }
                else
                {
                    ESP_LOGW(TAG, "apply_batch: fan_mode %d not supported by this unit, ignoring", (int)*fan_mode);
                }
            }
            if (swing_mode.has_value())
            {
                if (traits.supports_swing_mode(*swing_mode))
                {
                    this->swing_mode = *swing_mode;
                    changed = true;
                }
                else
                {
                    ESP_LOGW(TAG, "apply_batch: swing_mode %d not supported by this unit, ignoring", (int)*swing_mode);
                }
            }

            if (!changed)
            {
                ESP_LOGW(TAG, "apply_batch: no valid fields, nothing to send");
                return;
            }

            ESP_LOGI(TAG, "Applying batch climate command");
            this->transmit_state();
            this->publish_state();
        }
    } // namespace sharp
} // namespace esphome
