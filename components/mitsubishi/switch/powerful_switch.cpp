#include "powerful_switch.h"

namespace esphome
{
    namespace mitsubishi
    {
        void PowerfulSwitch::write_state(bool state)
        {
            this->publish_state(state);
            this->parent_->set_powerful(state);
        }
    } // namespace mitsubishi
} // namespace esphome
