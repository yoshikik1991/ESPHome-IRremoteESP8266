#include <cstring>

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
        // Ignore AEHA frames received within this long of our own transmission,
        // since the IR receiver picks up our own LED's reflection/crosstalk.
        static const uint32_t kRxLockoutMs = 500;

        static const char *const TAG = "fujitsu_264.climate";

        // ESPHome's built-in AEHAProtocol decoder reads bits MSB-first, but the
        // Fujitsu264 protocol actually transmits LSB-first (confirmed against a
        // real AR-RLB1J capture: the 5-byte "power off" frame's bit-reversal
        // matched byte-for-byte). So every value it hands us is bit-reversed
        // relative to the wire/raw[] representation and needs to be un-reversed:
        // the 16-bit address as a whole, and each data byte independently.
        static uint16_t reverse_bits16(uint16_t v)
        {
            uint16_t r = 0;
            for (int i = 0; i < 16; i++)
            {
                r = (r << 1) | (v & 1);
                v >>= 1;
            }
            return r;
        }

        static uint8_t reverse_bits8(uint8_t v)
        {
            v = ((v & 0xF0) >> 4) | ((v & 0x0F) << 4);
            v = ((v & 0xCC) >> 2) | ((v & 0x33) << 2);
            v = ((v & 0xAA) >> 1) | ((v & 0x55) << 1);
            return v;
        }

        void Fujitsu264Climate::setup()
        {
            climate_ir::ClimateIR::setup();
            this->apply_state();
        }

        void Fujitsu264Climate::set_fan_angle(const uint8_t fan_angle)
        {
            this->ac_.setFanAngle(fan_angle);
            ESP_LOGI(TAG, "Set fan angle to %d", fan_angle);
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
            this->ac_.setClean(clean);
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
            // No-op when unchanged. This also breaks the feedback loop where our
            // own transmission is picked up by the IR receiver, synced back into
            // the dry-mode select, and would otherwise be re-transmitted forever.
            if (this->weak_dry_ == weak_dry)
                return;
            this->weak_dry_ = weak_dry;
            ESP_LOGI(TAG, "Set weak dry to %s", weak_dry ? "ON" : "OFF");
            // retransmit only if the change is relevant now
            if (this->mode == climate::CLIMATE_MODE_DRY)
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
            this->last_tx_ms_ = millis();
        }

        bool Fujitsu264Climate::update_from_aeha(const uint16_t raw_address, const std::vector<uint8_t> &raw_data)
        {
            const uint16_t address = reverse_bits16(raw_address);
            if (address != kFujitsuAc264Address)
                return false;

            if (millis() - this->last_tx_ms_ < kRxLockoutMs)
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

            // Power-off is a short special frame with no mode/temp/fan payload.
            if (raw.size() == kFujitsuAc264StateLengthShort &&
                std::memcmp(raw.data(), kFujitsuAc264StatesTurnOff, kFujitsuAc264StateLengthShort) == 0)
            {
                ESP_LOGI(TAG, "Synced state from real remote: OFF");
                this->mode = climate::CLIMATE_MODE_OFF;
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
                this->fan_mode = climate::CLIMATE_FAN_QUIET;
                break;
            case kFujitsuAc264FanSpeedLow:
                this->fan_mode = climate::CLIMATE_FAN_LOW;
                break;
            case kFujitsuAc264FanSpeedMed:
                this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
                break;
            case kFujitsuAc264FanSpeedHigh:
                this->fan_mode = climate::CLIMATE_FAN_HIGH;
                break;
            default:
                this->fan_mode = climate::CLIMATE_FAN_AUTO;
                break;
            }

            this->target_temperature = this->ac_.getTemp();
            this->swing_mode = this->ac_.getSwing() ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;
            this->weak_dry_ = this->ac_.isWeakDry();

            ESP_LOGI(TAG, "Synced state from real remote: %s", this->ac_.toString().c_str());
            this->publish_state();
            return true;
        }

        void Fujitsu264Climate::apply_state()
        {
            if (this->mode == climate::CLIMATE_MODE_OFF)
            {
                this->ac_.off();
            }
            else
            {
                this->ac_.setTemp(this->target_temperature);

                if (this->fan_mode.has_value())
                {
                    switch (this->fan_mode.value())
                    {
                    case climate::CLIMATE_FAN_AUTO:
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedAuto);
                        break;
                    case climate::CLIMATE_FAN_QUIET:
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedQuiet);
                        break;
                    case climate::CLIMATE_FAN_LOW:
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedLow);
                        break;
                    case climate::CLIMATE_FAN_MEDIUM:
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedMed);
                        break;
                    case climate::CLIMATE_FAN_HIGH:
                        this->ac_.setFanSpeed(kFujitsuAc264FanSpeedHigh);
                        break;
                    default:
                        ESP_LOGW(TAG, "Unknown fan mode: %d", this->fan_mode.value());
                        break;
                    }
                }

                switch (this->swing_mode)
                {
                case climate::CLIMATE_SWING_OFF:
                    this->ac_.setSwing(false);
                    break;
                case climate::CLIMATE_SWING_VERTICAL:
                    this->ac_.setSwing(true);
                    break;
                default:
                    this->ac_.setSwing(true);
                    break;
                }

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
            }

            ESP_LOGI(TAG, "%s", this->ac_.toString().c_str());
        }

    }  // namespace fujitsu_264
}  // namespace esphome
