#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/helpers.h"
#include "../mitsubishi.h"

namespace esphome
{
    namespace mitsubishi
    {
        class CurrentCutSwitch : public switch_::Switch, public Parented<MitsubishiClimate>
        {
        public:
            CurrentCutSwitch() = default;

        protected:
            void write_state(bool state) override;
        };
    } // namespace mitsubishi
} // namespace esphome
