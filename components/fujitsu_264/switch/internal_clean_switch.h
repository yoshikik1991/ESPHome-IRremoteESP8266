#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "../fujitsu_264.h"

namespace esphome
{
    namespace fujitsu_264
    {
        class InternalCleanSwitch : public switch_::Switch, public Component, public Parented<Fujitsu264Climate>
        {
        public:
            void setup() override;

        protected:
            void write_state(bool state) override;
        };
    } // namespace fujitsu_264
} // namespace esphome
