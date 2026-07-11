#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "../mitsubishi.h"

namespace esphome
{
    namespace mitsubishi
    {
        class DryLevelSelect : public select::Select, public Component, public Parented<MitsubishiClimate>
        {
        public:
            void setup() override;

        protected:
            void control(size_t index) override;

        private:
            ESPPreferenceObject pref_;
        };
    } // namespace mitsubishi
} // namespace esphome
