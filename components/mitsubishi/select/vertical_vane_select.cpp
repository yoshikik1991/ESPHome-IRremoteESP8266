#include "vertical_vane_select.h"

namespace esphome
{
    namespace mitsubishi
    {
        void VerticalVaneSelect::setup()
        {
            this->pref_ = this->make_entity_preference<size_t>();
            size_t index = 0; // "自動"
            if (!this->pref_.load(&index) || !this->has_index(index))
                index = 0;
            this->publish_state(index);
            this->parent_->set_vertical_vane(index);
        }

        void VerticalVaneSelect::control(size_t index)
        {
            this->publish_state(index);
            this->pref_.save(&index);
            this->parent_->set_vertical_vane(index);
        }
    } // namespace mitsubishi
} // namespace esphome
