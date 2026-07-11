#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "../mitsubishi.h"

namespace esphome
{
    namespace mitsubishi
    {
        class InternalCleanSwitch : public switch_::Switch, public Component, public Parented<MitsubishiClimate>
        {
        public:
            void setup() override;

        protected:
            void write_state(bool state) override;
        };
    } // namespace mitsubishi
} // namespace esphome
