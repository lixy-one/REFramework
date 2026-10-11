// Localization.hpp — English → Simplified Chinese lookup for REFramework UI.
//
// 中文汉化机制：
//   1) 这个文件提供 l10n::tr("English")，查表返回中文；查不到就原样返回英文。
//   2) UI 代码里把字符串包一层： ImGui::Checkbox(l10n::tr("About"), ...)
//   3) 编译需要 /utf-8（见 CMakeLists.txt），否则源码里的中文字面量会乱码。
//
// 未翻译 / 不需要翻译的条目（路径、格式符、版本号等）会自然回退到英文。

#pragma once

namespace l10n {
    // 把英文 UI 字符串翻译成中文。找不到时返回原指针（零拷贝、无分配）。
    const char* tr(const char* en);

    // 是否已启用中文（留给未来做语言切换用）
    bool enabled();
    void set_enabled(bool v);
}
