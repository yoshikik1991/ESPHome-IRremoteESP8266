#include "internal_clean_switch.h"

namespace esphome
{
    namespace fujitsu_264
    {
        void InternalCleanSwitch::setup()
        {
            // get_initial_state_with_restore_mode() applies this switch's
            // configured restore_mode (RESTORE_DEFAULT_ON): restores the
            // persisted value across reboots, defaulting to ON on first boot.
            // If this entity isn't declared in a device's YAML at all, none of
            // this runs and Fujitsu264Climate::clean_ simply stays at its own
            // false/off field default.
            this->write_state(this->get_initial_state_with_restore_mode().value_or(false));
        }

        void InternalCleanSwitch::write_state(bool state)
        {
            this->publish_state(state);
            this->parent_->set_clean(state);
        }
    } // namespace fujitsu_264
} // namespace esphome
