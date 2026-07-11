#include "vertical_angle_select.h"

namespace esphome
{
    namespace fujitsu_264
    {
        void VerticalAngleSelect::setup()
        {
            this->pref_ = this->make_entity_preference<size_t>();
            size_t index = 0; // "1"
            if (!this->pref_.load(&index) || !this->has_index(index))
                index = 0;
            this->publish_state(index);
            this->parent_->set_vertical_angle(index + 1);
        }

        void VerticalAngleSelect::control(size_t index)
        {
            this->publish_state(index);
            this->pref_.save(&index);
            this->parent_->set_vertical_angle(index + 1);
        }
    } // namespace fujitsu_264
} // namespace esphome
