#include "dry_level_select.h"

namespace esphome
{
    namespace mitsubishi
    {
        void DryLevelSelect::setup()
        {
            this->pref_ = this->make_entity_preference<size_t>();
            size_t index = 1; // "標準"
            if (!this->pref_.load(&index) || !this->has_index(index))
                index = 1;
            this->publish_state(index);
            this->parent_->set_dry_level(index);
        }

        void DryLevelSelect::control(size_t index)
        {
            this->publish_state(index);
            this->pref_.save(&index);
            this->parent_->set_dry_level(index);
        }
    } // namespace mitsubishi
} // namespace esphome
