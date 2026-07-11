#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/helpers.h"
#include "../mitsubishi.h"

namespace esphome
{
    namespace mitsubishi
    {
        class PowerfulSwitch : public switch_::Switch, public Parented<MitsubishiClimate>
        {
        public:
            PowerfulSwitch() = default;

        protected:
            void write_state(bool state) override;
        };
    } // namespace mitsubishi
} // namespace esphome
