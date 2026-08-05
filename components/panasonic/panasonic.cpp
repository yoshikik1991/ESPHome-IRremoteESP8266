#include "esphome/core/helpers.h"
#include "esphome/components/remote_base/aeha_protocol.h"

#include "panasonic.h"

namespace esphome
{
    namespace panasonic
    {
        // copied from ir_Panasonic.cpp
        const uint16_t kPanasonicHdrMark = 3456;
        const uint16_t kPanasonicHdrSpace = 1728;
        const uint16_t kPanasonicBitMark = 432;
        const uint16_t kPanasonicOneSpace = 1296;
        const uint16_t kPanasonicZeroSpace = 432;
        const uint16_t kPanasonicAcSectionGap = 10000;
        const uint16_t kPanasonicAcSection1Length = 8;
        const uint32_t kPanasonicAcMessageGap = kDefaultMessageGap;

        // Section 1 of every valid frame is a constant 8 bytes (copied from
        // kPanasonicKnownGoodState in ir_Panasonic.h -- its first
        // kPanasonicAcSection1Length bytes). Used to reconstruct a full 27-byte
        // frame when only section 2 was decoded as its own receive event (see
        // update_from_aeha()).
        static const uint8_t kPanasonicSection1Prefix[kPanasonicAcSection1Length] = {
            0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06};

        static const char *const TAG = "panasonic.climate";

        // ESPHome's built-in AEHAProtocol decoder reads bits MSB-first. This
        // protocol is assumed to also transmit LSB-first on the wire, like
        // fujitsu_264 (confirmed there against a real capture) -- UNVERIFIED
        // here (no Panasonic hardware available), but IRremoteESP8266 sends this
        // whole protocol family LSB-first in general. Every value AEHAProtocol
        // hands us therefore needs un-reversing: the 16-bit address as a whole,
        // and each data byte independently (reverse_bits8/16 live on
        // IrRemoteBase).

        void PanasonicClimate::set_model(const Model model)
        {
            this->ac_.setModel((panasonic_ac_remote_model_t) model);
        }

        void PanasonicClimate::setup()
        {
            climate_ir::ClimateIR::setup();
            this->apply_state();
        }

        bool PanasonicClimate::on_receive(remote_base::RemoteReceiveData data)
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

        climate::ClimateTraits PanasonicClimate::traits()
        {
            auto traits = climate_ir::ClimateIR::traits();
            if (this->ac_.getModel() == panasonic_ac_remote_model_t::kPanasonicDke || this->ac_.getModel() == panasonic_ac_remote_model_t::kPanasonicRkr)
            {
                traits.add_supported_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);
                traits.add_supported_swing_mode(climate::CLIMATE_SWING_BOTH);
            }
            return traits;
        }

        void PanasonicClimate::transmit_state()
        {
            this->apply_state();

            uint8_t *message = this->ac_.getRaw();

            sendGeneric(
                kPanasonicHdrMark, kPanasonicHdrSpace,
                kPanasonicBitMark, kPanasonicOneSpace,
                kPanasonicBitMark, kPanasonicZeroSpace,
                kPanasonicBitMark, kPanasonicAcSectionGap,
                message, kPanasonicAcSection1Length,
                kPanasonicFreq
            );
            sendGeneric(
                kPanasonicHdrMark, kPanasonicHdrSpace,
                kPanasonicBitMark, kPanasonicOneSpace,
                kPanasonicBitMark, kPanasonicZeroSpace,
                kPanasonicBitMark, kPanasonicAcMessageGap,
                message + kPanasonicAcSection1Length,
                kPanasonicAcStateLength - kPanasonicAcSection1Length,
                kPanasonicFreq
            );
        }

        void PanasonicClimate::apply_state()
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
                    this->ac_.setMode(kPanasonicAcAuto);
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    this->ac_.setMode(kPanasonicAcHeat);
                    break;
                case climate::CLIMATE_MODE_COOL:
                    this->ac_.setMode(kPanasonicAcCool);
                    break;
                case climate::CLIMATE_MODE_DRY:
                    this->ac_.setMode(kPanasonicAcDry);
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    this->ac_.setMode(kPanasonicAcFan);
                    break;
                }

                if (this->fan_mode.has_value())
                {
                    switch (this->fan_mode.value())
                    {
                    case climate::CLIMATE_FAN_AUTO:
                        this->ac_.setFan(kPanasonicAcFanAuto);
                        break;
                    case climate::CLIMATE_FAN_QUIET:
                        this->ac_.setFan(kPanasonicAcFanMin);
                        break;
                    case climate::CLIMATE_FAN_LOW:
                        this->ac_.setFan(kPanasonicAcFanLow);
                        break;
                    case climate::CLIMATE_FAN_MEDIUM:
                        this->ac_.setFan(kPanasonicAcFanMed);
                        break;
                    case climate::CLIMATE_FAN_HIGH:
                        this->ac_.setFan(kPanasonicAcFanHigh);
                        break;
                    }
                }

                switch (this->swing_mode)
                {
                case climate::CLIMATE_SWING_OFF:
                    this->ac_.setSwingVertical(kPanasonicAcSwingVMiddle);
                    this->ac_.setSwingHorizontal(kPanasonicAcSwingHMiddle);
                    break;
                case climate::CLIMATE_SWING_VERTICAL:
                    this->ac_.setSwingVertical(kPanasonicAcSwingVAuto);
                    this->ac_.setSwingHorizontal(kPanasonicAcSwingHMiddle);
                    break;
                case climate::CLIMATE_SWING_HORIZONTAL:
                    this->ac_.setSwingVertical(kPanasonicAcSwingVMiddle);
                    this->ac_.setSwingHorizontal(kPanasonicAcSwingHAuto);
                    break;
                case climate::CLIMATE_SWING_BOTH:
                    this->ac_.setSwingVertical(kPanasonicAcSwingVAuto);
                    this->ac_.setSwingHorizontal(kPanasonicAcSwingHAuto);
                    break;
                }

                this->ac_.on();
            }

            ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
        }

        bool PanasonicClimate::update_from_aeha(const uint16_t raw_address, const std::vector<uint8_t> &raw_data)
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

            // Full wire-order dump of every frame/section from the real remote --
            // capture tool for mapping unknown buttons, same pattern as
            // fujitsu_264's own update_from_aeha().
            ESP_LOGD(TAG, "AEHA frame from remote (%u bytes): %s", raw.size(),
                     format_hex_pretty(raw.data(), raw.size()).c_str());

            std::vector<uint8_t> full;
            if (raw.size() == kPanasonicAcSection1Length)
            {
                // Section 1 alone (its own receive event, the common case given
                // the ~10ms inter-section gap): constant, carries no state.
                ESP_LOGD(TAG, "Panasonic AC section 1 observed (no state to sync)");
                return false;
            }
            else if (raw.size() == kPanasonicAcStateLength - kPanasonicAcSection1Length)
            {
                // Section 2 alone: prepend the constant section-1 prefix to
                // reconstruct a full frame.
                full.reserve(kPanasonicAcStateLength);
                full.insert(full.end(), kPanasonicSection1Prefix, kPanasonicSection1Prefix + kPanasonicAcSection1Length);
                full.insert(full.end(), raw.begin(), raw.end());
            }
            else if (raw.size() == kPanasonicAcStateLength)
            {
                // Both sections decoded as a single frame already (e.g. a
                // shorter idle threshold than the section gap).
                full = raw;
            }
            else
            {
                return false;
            }

            if (!IRPanasonicAc::validChecksum(full.data(), full.size()))
            {
                ESP_LOGW(TAG, "Ignoring AEHA frame with invalid checksum");
                return false;
            }

            this->ac_.setRaw(full.data());

            if (!this->ac_.getPower())
            {
                ESP_LOGI(TAG, "Synced state from real remote: OFF");
                this->mode = climate::CLIMATE_MODE_OFF;
                this->publish_state();
                return true;
            }

            switch (this->ac_.getMode())
            {
            case kPanasonicAcAuto:
                this->mode = climate::CLIMATE_MODE_HEAT_COOL;
                break;
            case kPanasonicAcCool:
                this->mode = climate::CLIMATE_MODE_COOL;
                break;
            case kPanasonicAcHeat:
                this->mode = climate::CLIMATE_MODE_HEAT;
                break;
            case kPanasonicAcDry:
                this->mode = climate::CLIMATE_MODE_DRY;
                break;
            case kPanasonicAcFan:
                this->mode = climate::CLIMATE_MODE_FAN_ONLY;
                break;
            default:
                ESP_LOGW(TAG, "Unknown mode in received frame: %d", this->ac_.getMode());
                return false;
            }

            this->target_temperature = this->ac_.getTemp();

            switch (this->ac_.getFan())
            {
            case kPanasonicAcFanAuto:
                this->fan_mode = climate::CLIMATE_FAN_AUTO;
                break;
            case kPanasonicAcFanMin:
                this->fan_mode = climate::CLIMATE_FAN_QUIET;
                break;
            case kPanasonicAcFanLow:
                this->fan_mode = climate::CLIMATE_FAN_LOW;
                break;
            case kPanasonicAcFanMed:
                this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
                break;
            case kPanasonicAcFanHigh:
                this->fan_mode = climate::CLIMATE_FAN_HIGH;
                break;
            default:
                this->fan_mode = climate::CLIMATE_FAN_AUTO;
                break;
            }

            // Horizontal swing is only meaningful on DKE/RKR models (see
            // traits()), but if the frame's own remote sent it, the physical
            // unit clearly has it -- sync it regardless of the configured model.
            const bool vertical_on = (this->ac_.getSwingVertical() == kPanasonicAcSwingVAuto);
            const bool horizontal_on = (this->ac_.getSwingHorizontal() == kPanasonicAcSwingHAuto);
            if (vertical_on && horizontal_on)
                this->swing_mode = climate::CLIMATE_SWING_BOTH;
            else if (vertical_on)
                this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
            else if (horizontal_on)
                this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
            else
                this->swing_mode = climate::CLIMATE_SWING_OFF;

            ESP_LOGI(TAG, "Synced state from real remote: %s", this->ac_.toString().c_str());
            this->publish_state();
            return true;
        }

        void PanasonicClimate::apply_batch(optional<climate::ClimateMode> mode,
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

    } // namespace panasonic_general
} // namespace esphome
