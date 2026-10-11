// Localization.hpp — runtime-loadable UI translations for REFramework.
//
// How it works
// ------------
//   1. A built-in English -> Simplified Chinese table is compiled into the DLL,
//      so a fresh install works out of the box.
//   2. On startup `reframework/langs/` is scanned for `*.json` files. Each file
//      declares its own display name and code, e.g. `zh-CN.json` -> "简体中文".
//      Files take priority over the built-in table and may add new languages.
//   3. UI code wraps strings in l10n::tr("English"). Unknown strings fall back
//      to the English original, so partial translations are fine.
//
// Adding a language
// -----------------
//   Drop a new `<code>.json` next to the others:
//
//     {
//       "Language": "日本語",
//       "Code": "ja-JP",
//       "Entries": { "Camera": "カメラ" }
//     }
//
//   No rebuild required - the file is picked up on the next launch, or
//   immediately after `l10n::reload()`.
//
// Note: the build must use /utf-8 (see CMakeLists.txt) or the Chinese literals
// in Localization.cpp come out mangled.

#pragma once

#include <string>
#include <vector>

namespace l10n {
    // One available language: either the built-in table or an external file.
    struct Language {
        std::string code;   // "zh-CN", "ja-JP", ... ("en" for the built-in English)
        std::string name;   // display name, e.g. "简体中文"
        std::string path;   // full path to the .json; empty for the built-in entry
    };

    // Translate an English UI string. Never returns nullptr and never allocates
    // on the hot path: unknown strings return `en` unchanged.
    const char* tr(const char* en);

    // -- lifecycle -----------------------------------------------------------
    // Scan <reframework>/langs/, load the saved language, and prepare the table.
    // Safe to call more than once; the second call is a no-op.
    void initialize();

    // Rescan the language directory and reload the current language.
    void reload();

    // -- selection -----------------------------------------------------------
    // All available languages, built-in English first.
    const std::vector<Language>& list();

    // Code of the active language ("en" when nothing is loaded).
    const std::string& current_code();

    // Display name of the active language.
    std::string current_name();

    // Switch language and reload the table. Returns false for unknown codes.
    bool set_current(const std::string& code);

    // -- helpers -------------------------------------------------------------
    // Write the built-in table to <reframework>/langs/zh-CN.json when that file
    // does not exist yet, so users have something to edit. Returns the path
    // that was written, or an empty string when nothing was written.
    std::string export_builtin();

    // True when a non-English language is active and the table is non-empty.
    bool enabled();

    // Legacy on/off switch kept for existing call sites: true selects the
    // built-in Chinese table, false selects English. New code should prefer
    // set_current("zh-CN") / set_current("en") - or any other language code.
    void set_enabled(bool v);
}
