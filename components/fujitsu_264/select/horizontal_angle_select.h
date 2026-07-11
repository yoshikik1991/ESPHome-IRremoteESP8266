#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "../fujitsu_264.h"

namespace esphome
{
    namespace fujitsu_264
    {
        class HorizontalAngleSelect : public select::Select, public Component, public Parented<Fujitsu264Climate>
        {
        public:
            void setup() override;

        protected:
            void control(size_t index) override;

        private:
            ESPPreferenceObject pref_;
        };
    } // namespace fujitsu_264
} // namespace esphome
