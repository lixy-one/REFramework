#include "../REFramework.hpp"
#include "../Localization.hpp"   // [CN patch]

#include "REFrameworkConfig.hpp"
#include "../Localization.hpp"   // [CN patch]

std::shared_ptr<REFrameworkConfig>& REFrameworkConfig::get() {
     static std::shared_ptr<REFrameworkConfig> instance{std::make_shared<REFrameworkConfig>()};
     return instance;
}

std::optional<std::string> REFrameworkConfig::on_initialize() {
    namespace fs = std::filesystem;
    fonts.clear();
    fonts.push_back("DEFAULT");

    const auto fonts_path = REFramework::get_persistent_dir() / "reframework" / "fonts";
    fs::create_directories(fonts_path);

    for (const auto& entry : fs::directory_iterator(fonts_path)) {
        if (fs::is_regular_file(entry)) {
            const auto ext = entry.path().extension();
            if (ext == ".otf" || ext == ".ttf") {
                fonts.push_back(entry.path().filename().string());
            }
        }
    }

    m_font_file = ModComboString::create(generate_name("FontFile"), fonts, "DEFAULT");

    g_framework->set_font(m_font_file->value());

    m_options.push_back(*m_font_file);

    // [CN patch] 应用界面语言（默认简体中文）
    l10n::set_enabled(m_language->value() != "English");

    return Mod::on_initialize();
}

void REFrameworkConfig::on_draw_ui() {
    if (!ImGui::CollapsingHeader(l10n::tr("Configuration"))) {
        return;
    }

    ImGui::TreePush("Configuration");

    bool changed = false;

    // [CN patch] 语言切换：切完立刻生效（l10n::tr 是运行时查表，下一帧 UI 即变）
    if (m_language->draw("Language")) {
        l10n::set_enabled(m_language->value() != "English");
        changed = true;
    }

    changed |= m_menu_key->draw(l10n::tr("Menu Key"));
    changed |= m_show_cursor_key->draw(l10n::tr("Show Cursor Key"));
    changed |= m_remember_menu_state->draw(l10n::tr("Remember Menu Open/Closed State"));
    changed |= m_always_show_cursor->draw(l10n::tr("Draw Cursor With Menu Open"));

    if (m_font_file->draw(l10n::tr("Font"))) {
        g_framework->set_font(m_font_file->value());
        changed = true;
    }

    if (m_font_size->draw(l10n::tr("Font Size"))) {
        g_framework->set_font_size(m_font_size->value());

        const auto display_size = g_framework->get_main_window_display_size();
        if (display_size.x > 0.0f && display_size.y > 0.0f) {
            set_ui_layout_state(
                static_cast<int32_t>(display_size.x),
                static_cast<int32_t>(display_size.y),
                g_framework->get_font_size());
        }

        changed = true;
    }

    if (changed) {
        g_framework->request_save_config();
    }

    ImGui::TreePop();
}

void REFrameworkConfig::on_frame() {
    if (m_show_cursor_key->is_key_down_once()) {
        m_always_show_cursor->toggle();
    }
}

void REFrameworkConfig::on_config_load(const utility::Config& cfg) {
    for (IModValue& option : m_options) {
        option.config_load(cfg);
    }

    if (m_remember_menu_state->value()) {
        g_framework->set_draw_ui(m_menu_open->value(), false);
    }
    
    g_framework->set_font(m_font_file->value());

    // [CN patch] 从配置恢复语言设置
    l10n::set_enabled(m_language->value() != "English");

    const auto saved_font_size = m_ui_font_size->value() > 0.0f
        ? m_ui_font_size->value()
        : static_cast<float>(m_font_size->value());
    g_framework->set_font_size_for_display(saved_font_size, static_cast<float>(m_ui_monitor_height->value()));
}

void REFrameworkConfig::on_config_save(utility::Config& cfg) {
    for (IModValue& option : m_options) {
        option.config_save(cfg);
    }
}
