#include "current_cut_switch.h"

namespace esphome
{
    namespace mitsubishi
    {
        void CurrentCutSwitch::write_state(bool state)
        {
            this->publish_state(state);
            this->parent_->set_current_cut(state);
        }
    } // namespace mitsubishi
} // namespace esphome
