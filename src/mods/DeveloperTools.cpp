#include "tools/GameObjectsDisplay.hpp"
#include "../Localization.hpp"   // [CN patch]
#include "tools/ChainViewer.hpp"
#include "../Localization.hpp"   // [CN patch]
#include "tools/ObjectExplorer.hpp"
#include "../Localization.hpp"   // [CN patch]

#include "DeveloperTools.hpp"
#include "../Localization.hpp"   // [CN patch]

DeveloperTools::DeveloperTools() {
    m_tools.emplace_back(std::make_shared<ChainViewer>());
    m_tools.emplace_back(std::make_shared<GameObjectsDisplay>());
    #ifndef _DEBUG
    // std::structs are not same as Release, this made crash
    m_tools.emplace_back(ObjectExplorer::get());
    #endif
}

void DeveloperTools::on_draw_ui() {
    if (!ImGui::CollapsingHeader(l10n::tr(get_name().data()))) {
        return;
    }

    for (auto& tool : m_tools) {
        tool->on_draw_dev_ui();
    }
}

