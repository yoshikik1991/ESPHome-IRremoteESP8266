#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "../fujitsu_264.h"

namespace esphome
{
    namespace fujitsu_264
    {
        class TempAutoOffsetNumber : public number::Number, public Component, public Parented<Fujitsu264Climate>
        {
        public:
            void setup() override;

        protected:
            void control(float value) override;

        private:
            ESPPreferenceObject pref_;
        };
    } // namespace fujitsu_264
} // namespace esphome
