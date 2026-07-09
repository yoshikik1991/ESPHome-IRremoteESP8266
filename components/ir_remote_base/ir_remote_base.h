#pragma once

#include <cstdint>
#include <vector>

#include "esphome/core/hal.h"
#include "esphome/components/climate_ir/climate_ir.h"

namespace esphome
{
    namespace ir_remote_base
    {
        class IrRemoteBase : public climate_ir::ClimateIR
        {
        public:
            IrRemoteBase(
                float minimum_temperature, float maximum_temperature, float temperature_step,
                bool supports_dry, bool supports_fan_only,
                climate::ClimateFanModeMask fan_modes,
                climate::ClimateSwingModeMask swing_modes)
                : ClimateIR(minimum_temperature, maximum_temperature, temperature_step,
                            supports_dry, supports_fan_only,
                            fan_modes, swing_modes) {}

            /// Reverse the bit order of a byte/word. Every component on this fork
            /// shares the same LSB-first-on-the-wire convention, while ESPHome's
            /// built-in AEHA decoder hands data back MSB-first -- subclasses whose
            /// receive-sync entry point is on_aeha need to undo that.
            static uint8_t reverse_bits8(uint8_t v)
            {
                v = ((v & 0xF0) >> 4) | ((v & 0x0F) << 4);
                v = ((v & 0xCC) >> 2) | ((v & 0x33) << 2);
                v = ((v & 0xAA) >> 1) | ((v & 0x55) << 1);
                return v;
            }

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

        protected:
            void sendGeneric(
                const uint16_t headermark, const uint32_t headerspace,
                const uint16_t onemark, const uint32_t onespace,
                const uint16_t zeromark, const uint32_t zerospace,
                const uint16_t footermark, const uint32_t gap,
                const uint8_t *message, const uint16_t nbytes,
                const uint16_t frequency)
            {
                auto transmit = this->transmitter_->transmit();
                auto *data = transmit.get_data();

                data->set_carrier_frequency(frequency);

                // Header
                if (headermark)
                    data->mark(headermark);
                else if (headerspace)
                    data->mark(1);
                if (headerspace)
                    data->space(headerspace);

                // Data
                for (uint16_t i = 0; i < nbytes; i++)
                {
                    sendData(data,
                             onemark, onespace,
                             zeromark, zerospace,
                             *(message + i), 8);
                }

                // Footer
                if (footermark)
                    data->mark(footermark);
                else if (gap)
                    data->mark(1);
                if (gap)
                    data->space(gap);

                transmit.perform();

                // Recorded so rx_locked_out() can tell a real remote's frame apart
                // from our own transmission bouncing back into the IR receiver.
                this->last_tx_ms_ = millis();
            }

            void sendData(
                esphome::remote_base::RemoteTransmitData *transmit_data,
                uint16_t onemark, uint32_t onespace,
                uint16_t zeromark, uint32_t zerospace,
                uint64_t data, uint16_t nbits)
            {
                for (uint16_t bit = 0; bit < nbits; bit++, data >>= 1)
                {
                    if (data & 1)
                    {
                        transmit_data->mark(onemark);
                        transmit_data->space(onespace);
                    }
                    else
                    {
                        transmit_data->mark(zeromark);
                        transmit_data->space(zerospace);
                    }
                }
            }

            /// True if we transmitted within the last window_ms -- receive-sync
            /// handlers should ignore frames in that window, since the IR receiver
            /// picks up our own LED's reflection/crosstalk as if it were a real
            /// remote button press.
            bool rx_locked_out(uint32_t window_ms = 500)
            {
                return millis() - this->last_tx_ms_ < window_ms;
            }

            /// Generic LSB-first pulse-train decoder for receive-sync entry points
            /// that work from raw pulses (protocols not covered by one of
            /// ESPHome's built-in decoders, e.g. remote_receiver's on_raw). Mirrors
            /// sendGeneric()'s own framing: one leader mark/space, then bytes as
            /// bit_mark+one_space/zero_space pairs, LSB first within each byte.
            /// Tolerance is +/-35% of each nominal duration (real-world capture
            /// jitter observed on this fork's supported units). Returns false and
            /// leaves out_bytes empty if the leader doesn't match or no complete
            /// byte could be decoded.
            static bool decode_pulses(
                const std::vector<int32_t> &pulses,
                uint16_t header_mark, uint16_t header_space,
                uint16_t bit_mark, uint16_t one_space, uint16_t zero_space,
                std::vector<uint8_t> &out_bytes)
            {
                out_bytes.clear();
                if (pulses.size() < 2)
                    return false;

                auto within = [](int32_t value, int32_t expected) {
                    const int32_t tolerance = expected * 35 / 100;
                    return value >= expected - tolerance && value <= expected + tolerance;
                };

                // pulses alternate mark (positive) / space (negative, as ESPHome's
                // on_raw hands them to us).
                if (!within(pulses[0], header_mark) || !within(-pulses[1], header_space))
                    return false;

                size_t idx = 2;
                while (idx + 1 < pulses.size())
                {
                    uint8_t byte_val = 0;
                    bool valid = true;
                    for (uint8_t bit = 0; bit < 8; bit++)
                    {
                        if (idx + 1 >= pulses.size() || !within(pulses[idx], bit_mark))
                        {
                            valid = false;
                            break;
                        }
                        const int32_t space = -pulses[idx + 1];
                        if (within(space, one_space))
                            byte_val |= (1 << bit);
                        else if (!within(space, zero_space))
                        {
                            valid = false;
                            break;
                        }
                        idx += 2;
                    }
                    if (!valid)
                        break;
                    out_bytes.push_back(byte_val);
                }

                return !out_bytes.empty();
            }

        private:
            /// Timestamp of our last transmission (set by sendGeneric()), used by
            /// rx_locked_out().
            uint32_t last_tx_ms_ = 0;
        };
    }
}
