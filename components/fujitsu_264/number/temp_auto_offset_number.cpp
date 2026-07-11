#include "temp_auto_offset_number.h"

namespace esphome
{
    namespace fujitsu_264
    {
        void TempAutoOffsetNumber::setup()
        {
            this->pref_ = this->make_entity_preference<float>();
            float value = 0.0f;
            if (!this->pref_.load(&value))
                value = 0.0f;
            this->publish_state(value);
            this->parent_->set_temp_auto_offset(value);
        }

        void TempAutoOffsetNumber::control(float value)
        {
            this->publish_state(value);
            this->pref_.save(&value);
            this->parent_->set_temp_auto_offset(value);
        }
    } // namespace fujitsu_264
} // namespace esphome
