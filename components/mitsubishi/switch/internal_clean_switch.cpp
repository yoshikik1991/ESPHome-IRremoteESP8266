#include "internal_clean_switch.h"

namespace esphome
{
    namespace mitsubishi
    {
        void InternalCleanSwitch::setup()
        {
            // get_initial_state_with_restore_mode() applies this switch's
            // configured restore_mode (RESTORE_DEFAULT_ON): restores the
            // persisted value across reboots, defaulting to ON on first boot.
            // If this entity isn't declared in a device's YAML at all, none of
            // this runs and MitsubishiClimate::clean_ simply stays at its own
            // false/off field default.
            this->write_state(this->get_initial_state_with_restore_mode().value_or(false));
        }

        void InternalCleanSwitch::write_state(bool state)
        {
            this->publish_state(state);
            // Note: MitsubishiClimate::set_clean() deliberately does not send()
            // immediately (confirmed on real hardware: the flag rides along on
            // whatever frame is next transmitted for another reason) -- that
            // timing is untouched here, only the entry point moved off YAML.
            this->parent_->set_clean(state);
        }
    } // namespace mitsubishi
} // namespace esphome
