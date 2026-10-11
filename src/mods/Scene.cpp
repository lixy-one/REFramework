#include <optional>
#include "../Localization.hpp"   // [CN patch]

#include "sdk/ReClass.hpp"
#include "../Localization.hpp"   // [CN patch]
#include "sdk/SceneManager.hpp"
#include "../Localization.hpp"   // [CN patch]
#include "sdk/Application.hpp"
#include "../Localization.hpp"   // [CN patch]

#include "Scene.hpp"
#include "../Localization.hpp"   // [CN patch]

std::optional<std::string> SceneMods::on_initialize() {
    return Mod::on_initialize();
}

void SceneMods::on_config_load(const utility::Config& cfg) {
    for (IModValue& option : m_options) {
        option.config_load(cfg);
    }
}

void SceneMods::on_config_save(utility::Config& cfg) {
    for (IModValue& option : m_options) {
        option.config_save(cfg);
    }
}

void SceneMods::on_frame() {
    if (m_set_timescale->value()) {
        if (m_use_application_timescale->value()) {
            sdk::Application::set_global_speed(m_timescale->value());
        } else {
            sdk::set_timescale(m_timescale->value());
        }
    }

    bool set_timescale = false;

    if (m_timescale_continuous_key->is_key_down()) {
        set_timescale = true;
        m_was_continuous_down = true;
        m_set_timescale->value() = true;
    } else if (m_was_continuous_down) {
        set_timescale = true;
        m_set_timescale->value() = false;
        m_was_continuous_down = false;
    }

    if (m_timescale_toggle_key->is_key_down_once()) {
        set_timescale = true;
        m_set_timescale->toggle();
    }

    if (set_timescale && !m_set_timescale->value()) {
        sdk::Application::set_global_speed(1.0f);
        sdk::set_timescale(1.0f);
    }
}

void SceneMods::on_draw_ui() {
    ImGui::SetNextItemOpen(false, ImGuiCond_::ImGuiCond_FirstUseEver);

    if (!ImGui::CollapsingHeader(l10n::tr(get_name().data()))) {
        return;
    }

    m_timescale_toggle_key->draw(l10n::tr("Timescale (Toggle) Key"));
    m_timescale_continuous_key->draw(l10n::tr("Timescale (Continuous) Key"));

    if (m_use_application_timescale->draw(l10n::tr("Use Application Timescale"))) {
        sdk::set_timescale(1.0f);
        sdk::Application::set_global_speed(1.0f);
    }

    m_timescale->draw(l10n::tr(""));
    ImGui::SameLine();

    if (m_set_timescale->draw(l10n::tr("Timescale"))) {
        if (!m_set_timescale->value()) {
            sdk::set_timescale(1.0f);
        }
    }
}
