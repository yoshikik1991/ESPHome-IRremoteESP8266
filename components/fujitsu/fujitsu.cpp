#include "esphome/core/helpers.h"
#include "esphome/components/remote_base/aeha_protocol.h"

#include "fujitsu.h"

namespace esphome
{
    namespace fujitsu
    {
        // copied from ir_Fujitsu.cpp
        const uint16_t kFujitsuAcHdrMark = 3324;
        const uint16_t kFujitsuAcHdrSpace = 1574;
        const uint16_t kFujitsuAcBitMark = 448;
        const uint16_t kFujitsuAcOneSpace = 1182;
        const uint16_t kFujitsuAcZeroSpace = 390;
        const uint16_t kFujitsuAcMinGap = 8100;

        // Fixed legacy-Fujitsu manufacturer header, as sent over the wire as the
        // AEHA "address" field (little-endian): raw[0]=0x14, raw[1]=0x63. Same
        // vendor header as fujitsu_264 -- this is the older/shorter sibling
        // protocol (see fujitsu_264's own kFujitsuAc264Address for the AC264
        // variant of this same header).
        static const uint16_t kFujitsuAcAddress = 0x6314;

        static const char *const TAG = "fujitsu.climate";

        // ESPHome's built-in AEHAProtocol decoder reads bits MSB-first, but this
        // protocol (like fujitsu_264) is assumed to transmit LSB-first on the
        // wire -- UNVERIFIED here (no hardware to capture against), but
        // consistent with fujitsu_264's confirmed behavior for the same vendor
        // header and with IRremoteESP8266 sending this whole protocol family
        // LSB-first in general. Every value AEHAProtocol hands us therefore needs
        // un-reversing: the 16-bit address as a whole, and each data byte
        // independently (reverse_bits8/16 live on IrRemoteBase).

        void FujitsuClimate::set_model(const Model model)
        {
            this->ac_.setModel((fujitsu_ac_remote_model_t) model);
        }

        void FujitsuClimate::setup()
        {
            climate_ir::ClimateIR::setup();
            this->apply_state();
        }

        bool FujitsuClimate::on_receive(remote_base::RemoteReceiveData data)
        {
            // Entry point for receive-sync: mirrors fujitsu_264's on_receive()
            // exactly (single AEHAProtocol().decode() attempt -> update_from_aeha()).
            // Requires `receiver_id:` to be set on this climate's YAML config,
            // otherwise ClimateIR never registers us as a RemoteReceiverListener
            // at all.
            auto aeha = remote_base::AEHAProtocol().decode(data);
            if (!aeha.has_value())
                return false;
            return this->update_from_aeha(aeha->address, aeha->data);
        }

        climate::ClimateTraits FujitsuClimate::traits()
        {
            auto traits = climate_ir::ClimateIR::traits();
            if (this->supports_horizontal_swing())
            {
                traits.add_supported_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);
                traits.add_supported_swing_mode(climate::CLIMATE_SWING_BOTH);
            }
            if (this->supports_econo_powerful())
            {
                traits.set_supported_presets({
                    climate::CLIMATE_PRESET_NONE,
                    climate::CLIMATE_PRESET_ECO,
                    climate::CLIMATE_PRESET_BOOST,
                });
            }
            return traits;
        }

        void FujitsuClimate::transmit_state()
        {
            this->apply_state();
            this->send();
        }

        void FujitsuClimate::control(const climate::ClimateCall &call)
        {
            // Intercept preset changes BEFORE delegating to the parent so that
            // we send the toggle command(s) for Eco / Powerful as needed.
            // Parent's control() will then transmit the regular state frame
            // for any other change (mode, temp, fan, swing).
            if (call.get_preset().has_value())
            {
                auto desired_preset = *call.get_preset();
                bool desired_econo = (desired_preset == climate::CLIMATE_PRESET_ECO);
                bool desired_powerful = (desired_preset == climate::CLIMATE_PRESET_BOOST);

                if (desired_econo != this->econo_state_)
                    this->toggle_econo();
                if (desired_powerful != this->powerful_state_)
                    this->toggle_powerful();
            }

            climate_ir::ClimateIR::control(call);
        }

        void FujitsuClimate::step_horizontal()
        {
            if (this->supports_horizontal_swing())
            {
                this->ac_.stepHoriz();
                ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
                this->send();
            }
            else
            {
                ESP_LOGW(TAG, "Model does not support horizontal swing");
            }
        }

        void FujitsuClimate::step_vertical()
        {
            this->ac_.stepVert();
            ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
            this->send();
        }

        void FujitsuClimate::toggle_econo()
        {
            if (this->supports_econo_powerful())
            {
                this->ac_.setCmd(kFujitsuAcCmdEcono);
                this->send();
                this->econo_state_ = !this->econo_state_;
                this->powerful_state_ = false;
                ESP_LOGI(TAG, "Toggling Eco (now %s)", this->econo_state_ ? "ON" : "OFF");
                this->sync_preset_to_state();
            }
            else
            {
                ESP_LOGW(TAG, "Model does not support econo mode");
            }
        }

        void FujitsuClimate::toggle_powerful()
        {
            if (this->supports_econo_powerful())
            {
                this->ac_.setCmd(kFujitsuAcCmdPowerful);
                this->send();
                this->powerful_state_ = !this->powerful_state_;
                this->econo_state_ = false;
                ESP_LOGI(TAG, "Toggling Powerful (now %s)", this->powerful_state_ ? "ON" : "OFF");
                this->sync_preset_to_state();
            }
            else
            {
                ESP_LOGW(TAG, "Model does not support powerful mode");
            }
        }

        // Reflect the internal Eco / Powerful tracking into HA's preset field.
        // ECO and BOOST are mutually exclusive in HA's preset model, so when both
        // are internally on we prefer ECO (last-toggled-wins is harder to track
        // and not particularly useful — Eco wins as the more common everyday mode).
        void FujitsuClimate::sync_preset_to_state()
        {
            if (this->econo_state_)
                this->preset = climate::CLIMATE_PRESET_ECO;
            else if (this->powerful_state_)
                this->preset = climate::CLIMATE_PRESET_BOOST;
            else
                this->preset = climate::CLIMATE_PRESET_NONE;
            this->publish_state();
        }

        void FujitsuClimate::send()
        {
            uint8_t *message = this->ac_.getRaw();
            uint8_t length = this->ac_.getStateLength();

            sendGeneric(
                kFujitsuAcHdrMark, kFujitsuAcHdrSpace,
                kFujitsuAcBitMark, kFujitsuAcOneSpace,
                kFujitsuAcBitMark, kFujitsuAcZeroSpace,
                kFujitsuAcBitMark, kFujitsuAcMinGap,
                message, length,
                38000
            );
        }

        void FujitsuClimate::apply_state()
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
                    this->ac_.setMode(kFujitsuAcModeAuto);
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    this->ac_.setMode(kFujitsuAcModeHeat);
                    break;
                case climate::CLIMATE_MODE_COOL:
                    this->ac_.setMode(kFujitsuAcModeCool);
                    break;
                case climate::CLIMATE_MODE_DRY:
                    this->ac_.setMode(kFujitsuAcModeDry);
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    this->ac_.setMode(kFujitsuAcModeFan);
                    break;
                }

                if (this->fan_mode.has_value())
                {
                    switch (this->fan_mode.value())
                    {
                    case climate::CLIMATE_FAN_AUTO:
                        this->ac_.setFanSpeed(kFujitsuAcFanAuto);
                        break;
                    case climate::CLIMATE_FAN_QUIET:
                        this->ac_.setFanSpeed(kFujitsuAcFanQuiet);
                        break;
                    case climate::CLIMATE_FAN_LOW:
                        this->ac_.setFanSpeed(kFujitsuAcFanLow);
                        break;
                    case climate::CLIMATE_FAN_MEDIUM:
                        this->ac_.setFanSpeed(kFujitsuAcFanMed);
                        break;
                    case climate::CLIMATE_FAN_HIGH:
                        this->ac_.setFanSpeed(kFujitsuAcFanHigh);
                        break;
                    }
                }

                switch (this->swing_mode)
                {
                case climate::CLIMATE_SWING_OFF:
                    this->ac_.setSwing(kFujitsuAcSwingOff);
                    break;
                case climate::CLIMATE_SWING_VERTICAL:
                    this->ac_.setSwing(kFujitsuAcSwingVert);
                    break;
                case climate::CLIMATE_SWING_HORIZONTAL:
                    this->ac_.setSwing(kFujitsuAcSwingHoriz);
                    break;
                case climate::CLIMATE_SWING_BOTH:
                    this->ac_.setSwing(kFujitsuAcSwingBoth);
                    break;
                }

                this->ac_.on();
            }

            ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
        }

        bool FujitsuClimate::update_from_aeha(const uint16_t raw_address, const std::vector<uint8_t> &raw_data)
        {
            if (this->rx_locked_out())
            {
                ESP_LOGD(TAG, "Ignoring AEHA frame received shortly after our own transmission");
                return false;
            }

            const uint16_t address = reverse_bits16(raw_address);
            std::vector<uint8_t> raw;
            raw.reserve(2 + raw_data.size());
            raw.push_back(static_cast<uint8_t>(address & 0xFF));
            raw.push_back(static_cast<uint8_t>((address >> 8) & 0xFF));
            for (uint8_t b : raw_data)
                raw.push_back(reverse_bits8(b));

            // Full wire-order dump of every frame from the real remote, including
            // ones from addresses we don't recognize -- capture tool for mapping
            // unknown buttons, same pattern as fujitsu_264's own
            // update_from_aeha().
            ESP_LOGD(TAG, "AEHA frame from remote (%u bytes): %s", raw.size(),
                     format_hex_pretty(raw.data(), raw.size()).c_str());

            if (address != kFujitsuAcAddress)
                return false;

            // ARDB1/ARJW2 use one-byte-shorter long/short codes (kFujitsuAcStateLength-1
            // / kFujitsuAcStateLengthShort-1, i.e. 15/6 bytes) -- not handled here,
            // since no ARDB1/ARJW2 hardware was available to confirm the mapping.
            // This component's default/tested config (ARRAH2E-family: ARRAH2E,
            // ARREB1E, ARRY4, ARREW4E) always uses the 16/7-byte lengths below.
            if (raw.size() != kFujitsuAcStateLength && raw.size() != kFujitsuAcStateLengthShort)
                return false;

            if (!IRFujitsuAC::validChecksum(raw.data(), raw.size()))
            {
                ESP_LOGW(TAG, "Ignoring AEHA frame with invalid checksum");
                return false;
            }

            // IRFujitsuAC::setRaw() -> buildFromState() heuristically re-detects
            // the model from the frame's own header bytes (RestLength/Protocol/
            // OutsideQuiet for long/16-byte frames; just the Cmd byte for
            // short/7-byte ones). Short toggle frames carry none of the
            // long-frame header info (setRaw() zero-pads every byte beyond
            // `length`), so the heuristic is coarse there and can misdetect a
            // model other than the one this component was explicitly configured
            // for via set_model() -- silently changing state length / per-model
            // quirks (horizontal swing support, econo/powerful availability, ...)
            // for every future transmission. setModel() only touches that
            // bookkeeping (_model/_state_length/_state_length_short), never the
            // frame bytes themselves, so save/restore it around setRaw() to undo
            // any such reclassification without losing any of the parsed fields.
            // (Chosen over reimplementing a raw-byte comparison for short frames:
            // this covers both short AND long frames uniformly and is far
            // simpler, at the cost of one redundant setModel() call.)
            const auto configured_model = this->ac_.getModel();
            const bool ok = this->ac_.setRaw(raw.data(), raw.size());
            this->ac_.setModel(configured_model);
            if (!ok)
                return false;

            if (!this->ac_.getPower())
            {
                ESP_LOGI(TAG, "Synced state from real remote: OFF");
                this->mode = climate::CLIMATE_MODE_OFF;
                this->publish_state();
                return true;
            }

            if (raw.size() == kFujitsuAcStateLengthShort)
            {
                // Short frames that aren't power-off (econo/powerful toggles,
                // step-vane commands) carry no absolute mode/temp/fan/swing
                // payload to reflect onto the climate entity -- and this
                // component only best-effort *tracks* econo_state_/
                // powerful_state_ from ESPHome-initiated toggles (see the class
                // doc comment), so it can't derive their true state from a stray
                // remote press either. Just log what command class it was, at
                // DEBUG, and leave climate state untouched.
                ESP_LOGD(TAG, "Observed a short command frame (Cmd=0x%02X) from the real "
                              "remote; no absolute state to sync from it",
                         this->ac_.getCmd());
                return false;
            }

            switch (this->ac_.getMode())
            {
            case kFujitsuAcModeAuto:
                this->mode = climate::CLIMATE_MODE_HEAT_COOL;
                break;
            case kFujitsuAcModeCool:
                this->mode = climate::CLIMATE_MODE_COOL;
                break;
            case kFujitsuAcModeHeat:
                this->mode = climate::CLIMATE_MODE_HEAT;
                break;
            case kFujitsuAcModeDry:
                this->mode = climate::CLIMATE_MODE_DRY;
                break;
            case kFujitsuAcModeFan:
                this->mode = climate::CLIMATE_MODE_FAN_ONLY;
                break;
            default:
                ESP_LOGW(TAG, "Unknown mode in received frame: %d", this->ac_.getMode());
                return false;
            }

            this->target_temperature = this->ac_.getTemp();

            switch (this->ac_.getFanSpeed())
            {
            case kFujitsuAcFanAuto:
                this->fan_mode = climate::CLIMATE_FAN_AUTO;
                break;
            case kFujitsuAcFanQuiet:
                this->fan_mode = climate::CLIMATE_FAN_QUIET;
                break;
            case kFujitsuAcFanLow:
                this->fan_mode = climate::CLIMATE_FAN_LOW;
                break;
            case kFujitsuAcFanMed:
                this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
                break;
            case kFujitsuAcFanHigh:
                this->fan_mode = climate::CLIMATE_FAN_HIGH;
                break;
            default:
                ESP_LOGW(TAG, "Unknown fan speed in received frame: %d", this->ac_.getFanSpeed());
                break;
            }

            switch (this->ac_.getSwing())
            {
            case kFujitsuAcSwingOff:
                this->swing_mode = climate::CLIMATE_SWING_OFF;
                break;
            case kFujitsuAcSwingVert:
                this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
                break;
            case kFujitsuAcSwingHoriz:
                // Horizontal swing is model-dependent (supports_horizontal_swing()
                // above only allows it for ARRAH2E/ARJW2), but if the frame's own
                // remote sent it, the physical unit clearly has it -- sync it
                // regardless of the configured model/override.
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
                break;
            case kFujitsuAcSwingBoth:
                this->swing_mode = climate::CLIMATE_SWING_BOTH;
                break;
            default:
                ESP_LOGW(TAG, "Unknown swing setting in received frame: %d", this->ac_.getSwing());
                break;
            }

            ESP_LOGI(TAG, "Synced state from real remote: %s", this->ac_.toString().c_str());
            this->publish_state();
            return true;
        }

        void FujitsuClimate::apply_batch(optional<climate::ClimateMode> mode,
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

    } // namespace fujitsu_general
} // namespace esphome
