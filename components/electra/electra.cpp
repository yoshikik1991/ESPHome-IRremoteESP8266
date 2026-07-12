#include "esphome/core/helpers.h"

#include "electra.h"

namespace esphome
{
    namespace electra
    {
        // copied from ir_Electra.cpp
        const uint16_t kElectraAcHdrMark = 9166;
        const uint16_t kElectraAcBitMark = 646;
        const uint16_t kElectraAcHdrSpace = 4470;
        const uint16_t kElectraAcOneSpace = 1647;
        const uint16_t kElectraAcZeroSpace = 547;
        const uint32_t kElectraAcMessageGap = kDefaultMessageGap;  // Just a guess.

        static const char *const TAG = "electra.climate";

        void ElectraClimate::setup()
        {
            climate_ir::ClimateIR::setup();
            this->apply_state();
        }

        bool ElectraClimate::on_receive(remote_base::RemoteReceiveData data)
        {
            // Entry point for receive-sync. Electra uses a distinct long-leader
            // timing family (9166/4470), not AEHA's (3400/1700), so unlike
            // mitsubishi's dual-path on_receive() (which must try AEHA first for
            // its short side-channel toggle frames) this only ever needs the raw
            // pulse path. Requires `receiver_id:` to be set on this climate's
            // YAML config, otherwise ClimateIR never registers us as a
            // RemoteReceiverListener at all.
            data.reset();
            return this->update_from_raw(data.get_raw_data());
        }

        void ElectraClimate::transmit_state()
        {
            this->apply_state();
            this->send();
        }

        void ElectraClimate::send()
        {
            uint8_t *message = this->ac_.getRaw();

            sendGeneric(
                kElectraAcHdrMark, kElectraAcHdrSpace,
                kElectraAcBitMark, kElectraAcOneSpace,
                kElectraAcBitMark, kElectraAcZeroSpace,
                kElectraAcBitMark, kElectraAcMessageGap,
                message, kElectraAcStateLength,
                38000
            );
        }

        void ElectraClimate::apply_state()
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
                    this->ac_.setMode(kElectraAcFanAuto);
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    this->ac_.setMode(kElectraAcHeat);
                    break;
                case climate::CLIMATE_MODE_COOL:
                    this->ac_.setMode(kElectraAcCool);
                    break;
                case climate::CLIMATE_MODE_DRY:
                    this->ac_.setMode(kElectraAcDry);
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    this->ac_.setMode(kElectraAcFan);
                    break;
                }

                if (this->fan_mode.has_value())
                {
                    switch (this->fan_mode.value())
                    {
                    case climate::CLIMATE_FAN_AUTO:
                        this->ac_.setFan(kElectraAcFanAuto);
                        break;
                    case climate::CLIMATE_FAN_LOW:
                        this->ac_.setFan(kElectraAcFanLow);
                        break;
                    case climate::CLIMATE_FAN_MEDIUM:
                        this->ac_.setFan(kElectraAcFanMed);
                        break;
                    case climate::CLIMATE_FAN_HIGH:
                        this->ac_.setFan(kElectraAcFanHigh);
                        break;
                    }
                }

                switch (this->swing_mode)
                {
                case climate::CLIMATE_SWING_OFF:
                    this->ac_.setSwingH(false);
                    this->ac_.setSwingV(false);
                    break;
                case climate::CLIMATE_SWING_VERTICAL:
                    this->ac_.setSwingH(false);
                    this->ac_.setSwingV(true);
                    break;
                case climate::CLIMATE_SWING_HORIZONTAL:
                    this->ac_.setSwingH(true);
                    this->ac_.setSwingV(false);
                    break;
                case climate::CLIMATE_SWING_BOTH:
                    this->ac_.setSwingH(true);
                    this->ac_.setSwingV(true);
                    break;
                }

                this->ac_.on();
            }

            ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
        }

        bool ElectraClimate::update_from_raw(const std::vector<int32_t> &pulses)
        {
            if (this->rx_locked_out())
            {
                ESP_LOGD(TAG, "Ignoring raw frame received shortly after our own transmission");
                return false;
            }

            std::vector<uint8_t> raw;
            if (!decode_pulses(pulses,
                                kElectraAcHdrMark, kElectraAcHdrSpace,
                                kElectraAcBitMark, kElectraAcOneSpace, kElectraAcZeroSpace,
                                raw))
            {
                ESP_LOGD(TAG, "Raw frame (%u pulses) did not match Electra timing", pulses.size());
                return false;
            }

            // Full dump of every frame matching this timing family -- capture
            // tool for mapping unknown buttons, same pattern as mitsubishi's own
            // update_from_raw()/update_from_aeha().
            ESP_LOGD(TAG, "Raw frame (%u bytes): %s", raw.size(),
                     format_hex_pretty(raw.data(), raw.size()).c_str());

            if (raw.size() != kElectraAcStateLength)
                return false;

            if (!IRElectraAc::validChecksum(raw.data(), raw.size()))
            {
                ESP_LOGW(TAG, "Ignoring raw frame with invalid checksum");
                return false;
            }

            this->ac_.setRaw(raw.data(), raw.size());

            if (!this->ac_.getPower())
            {
                this->mode = climate::CLIMATE_MODE_OFF;
            }
            else
            {
                switch (this->ac_.getMode())
                {
                // Mirrors apply_state()'s own (pre-existing) mode mapping
                // exactly, including its use of CLIMATE_MODE_AUTO rather than
                // CLIMATE_MODE_HEAT_COOL for kElectraAcAuto -- not this
                // receive-sync addition's place to relitigate that.
                case kElectraAcAuto:
                    this->mode = climate::CLIMATE_MODE_AUTO;
                    break;
                case kElectraAcCool:
                    this->mode = climate::CLIMATE_MODE_COOL;
                    break;
                case kElectraAcHeat:
                    this->mode = climate::CLIMATE_MODE_HEAT;
                    break;
                case kElectraAcDry:
                    this->mode = climate::CLIMATE_MODE_DRY;
                    break;
                case kElectraAcFan:
                    this->mode = climate::CLIMATE_MODE_FAN_ONLY;
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown mode in received frame: %d", this->ac_.getMode());
                    return false;
                }

                this->target_temperature = this->ac_.getTemp();

                switch (this->ac_.getFan())
                {
                case kElectraAcFanAuto:
                    this->fan_mode = climate::CLIMATE_FAN_AUTO;
                    break;
                case kElectraAcFanLow:
                    this->fan_mode = climate::CLIMATE_FAN_LOW;
                    break;
                case kElectraAcFanMed:
                    this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
                    break;
                case kElectraAcFanHigh:
                    this->fan_mode = climate::CLIMATE_FAN_HIGH;
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown fan speed in received frame: %d", this->ac_.getFan());
                    break;
                }

                const bool vertical_on = this->ac_.getSwingV();
                const bool horizontal_on = this->ac_.getSwingH();
                if (vertical_on && horizontal_on)
                    this->swing_mode = climate::CLIMATE_SWING_BOTH;
                else if (vertical_on)
                    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
                else if (horizontal_on)
                    this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
                else
                    this->swing_mode = climate::CLIMATE_SWING_OFF;
            }

            ESP_LOGI(TAG, "Synced state from real remote: %s", this->ac_.toString().c_str());
            this->publish_state();
            return true;
        }
    } // namespace electra
} // namespace esphome
