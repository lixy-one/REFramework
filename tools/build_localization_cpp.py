"""Rebuild src/Localization.cpp as a runtime-loadable table.

The 571 built-in entries are carried over verbatim from the previous version of
the file, so the default (Simplified Chinese) behaviour is unchanged. Around
them we add:

  * <reframework>/langs/*.json loading (external files win over built-in data)
  * language list / selection / reload
  * export of the built-in table so users have a file to edit

Run from the repo root:  python tools/build_localization_cpp.py
"""

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "src", "Localization.cpp")
HOOK_DST = os.path.join(ROOT, "src", "Localization_sections.inc")

ENTRY_RE = re.compile(r'^\s*\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,?\s*$')

HEADER = '''// Localization.cpp — runtime-loadable UI translation table.
//
// Lookup order for l10n::tr("English"):
//
//   1. English is active                -> the original string, untouched.
//   2. An external file defines the key -> that translation wins.
//   3. Built-in table has the key       -> used when the active language is
//                                          the one the table was written for.
//   4. Otherwise                        -> the original English string.
//
// The built-in table below is the original Simplified Chinese translation, so a
// fresh install keeps working with no files on disk. Everything else is loaded
// from <reframework>/langs/*.json at startup and can be edited without a
// rebuild.
//
// NOTE: this file must be compiled with /utf-8 (see CMakeLists.txt), otherwise
// the Chinese literals below are mangled.

#include "Localization.hpp"

#include "REFramework.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <json.hpp>

namespace l10n {
    namespace {
        // ---------------------------------------------------------------- data

        // Language code the built-in table was written for.
        constexpr const char* kBuiltinCode = "zh-CN";

        // Built-in English -> Simplified Chinese. Keys are string literals, so
        // string_view keys are safe and lookups cost no allocation.
        const std::unordered_map<std::string_view, const char*> kBuiltin = {
'''

FOOTER = '''        };

        // Loaded from disk; empty when English is active or no file matched.
        std::unordered_map<std::string, std::string> g_external{};
        std::vector<Language> g_languages{};
        std::string g_current = "en";
        bool g_attempted_init = false;
        std::mutex g_mutex{};

        // ------------------------------------------------------------- helpers

        std::filesystem::path langs_dir() {
            return REFramework::get_persistent_dir() / "reframework" / "langs";
        }

        std::string read_file(const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                return {};
            }

            std::string out;
            in.seekg(0, std::ios::end);
            const auto size = in.tellg();
            if (size > 0) {
                out.resize(static_cast<size_t>(size));
                in.seekg(0, std::ios::beg);
                in.read(out.data(), size);
            }

            // Strip a UTF-8 BOM - editors on Windows like to add one.
            if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
                static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
                out.erase(0, 3);
            }

            return out;
        }

        std::string json_escape(const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (const char c : in) {
                switch (c) {
                case '"':  out += "\\\\\\""; break;
                case '\\\\': out += "\\\\\\\\"; break;
                case '\\n': out += "\\\\n";   break;
                case '\\r': out += "\\\\r";   break;
                case '\\t': out += "\\\\t";   break;
                default:   out += c;       break;
                }
            }
            return out;
        }

        // -------------------------------------------------------------- loading

        // Load one language file into g_external. Returns false when the JSON is
        // malformed or has no usable entries.
        bool load_external(const std::filesystem::path& path, std::string* out_name) {
            const auto text = read_file(path);
            if (text.empty()) {
                return false;
            }

            nlohmann::json doc;
            try {
                doc = nlohmann::json::parse(text);
            } catch (...) {
                return false;
            }

            if (!doc.is_object()) {
                return false;
            }

            const auto entries = doc.find("Entries");
            if (entries == doc.end() || !entries->is_object()) {
                return false;
            }

            std::unordered_map<std::string, std::string> loaded{};
            loaded.reserve(entries->size());

            for (auto it = entries->begin(); it != entries->end(); ++it) {
                if (!it->is_string()) {
                    continue;
                }
                loaded.emplace(it.key(), it->get<std::string>());
            }

            if (loaded.empty()) {
                return false;
            }

            if (out_name != nullptr) {
                const auto name = doc.find("Language");
                if (name != doc.end() && name->is_string()) {
                    *out_name = name->get<std::string>();
                } else {
                    *out_name = path.stem().string();
                }
            }

            g_external = std::move(loaded);
            return true;
        }

        void scan_language_files() {
            g_languages.clear();
            g_languages.push_back(Language{"en", "English", ""});

            std::error_code ec{};
            const auto dir = langs_dir();

            if (!std::filesystem::is_directory(dir, ec)) {
                return;
            }

            std::vector<std::filesystem::path> files{};
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file(ec)) {
                    continue;
                }
                if (entry.path().extension() != ".json") {
                    continue;
                }
                files.push_back(entry.path());
            }

            std::sort(files.begin(), files.end());

            for (const auto& path : files) {
                const auto text = read_file(path);
                if (text.empty()) {
                    continue;
                }

                nlohmann::json doc;
                try {
                    doc = nlohmann::json::parse(text);
                } catch (...) {
                    continue;
                }
                if (!doc.is_object()) {
                    continue;
                }

                Language lang{};
                lang.code = path.stem().string();
                lang.path = path.string();
                lang.name = lang.code;

                const auto name = doc.find("Language");
                if (name != doc.end() && name->is_string()) {
                    lang.name = name->get<std::string>();
                }

                if (lang.code == "en") {
                    continue; // English is always the built-in fallback.
                }

                // Later files with the same stem override earlier ones.
                const auto existing = std::find_if(g_languages.begin(), g_languages.end(),
                    [&](const Language& l) { return l.code == lang.code; });
                if (existing != g_languages.end()) {
                    *existing = lang;
                } else {
                    g_languages.push_back(lang);
                }
            }
        }

        const Language* find_language(const std::string& code) {
            const auto it = std::find_if(g_languages.begin(), g_languages.end(),
                [&](const Language& l) { return l.code == code; });
            return it == g_languages.end() ? nullptr : &(*it);
        }

        // (Re)build g_external for the given code.
        void apply_language(const std::string& code) {
            g_external.clear();
            g_current = "en";

            if (code == "en") {
                return;
            }

            const auto* lang = find_language(code);
            if (lang == nullptr) {
                return;
            }

            // Built-in table stands in for its own language when no file exists.
            if (!lang->path.empty()) {
                std::string name{};
                if (!load_external(lang->path, &name)) {
                    return;
                }
            }

            g_current = code;
        }

        std::string read_langs_dir_text() {
            return langs_dir().string();
        }
    } // namespace

    // ---------------------------------------------------------------- lookup

    const char* tr(const char* en) {
        if (en == nullptr) {
            return "";
        }

        if (g_current == "en") {
            return en;
        }

        if (!g_external.empty()) {
            const auto it = g_external.find(en);
            if (it != g_external.end()) {
                return it->second.c_str();
            }
        }

        if (g_current == kBuiltinCode) {
            const auto it = kBuiltin.find(std::string_view{en});
            if (it != kBuiltin.end()) {
                return it->second;
            }
        }

        return en;
    }

    // ------------------------------------------------------------- lifecycle

    void initialize() {
        std::lock_guard<std::mutex> lock{g_mutex};
        if (g_attempted_init) {
            return;
        }
        g_attempted_init = true;

        scan_language_files();

        // Pick up a previously saved choice, if the file is still there.
        std::string wanted{};
        try {
            const auto cfg = REFramework::get_persistent_dir() / "reframework" / "data" / "l10n.json";
            const auto text = read_file(cfg);
            if (!text.empty()) {
                const auto doc = nlohmann::json::parse(text);
                const auto it = doc.find("Language");
                if (it != doc.end() && it->is_string()) {
                    wanted = it->get<std::string>();
                }
            }
        } catch (...) {
        }

        apply_language(wanted);
    }

    void reload() {
        std::lock_guard<std::mutex> lock{g_mutex};
        const auto keep = g_current;
        scan_language_files();
        apply_language(keep);

        // Remember the choice so the next launch starts in the same language.
        try {
            const auto dir = REFramework::get_persistent_dir() / "reframework" / "data";
            std::error_code ec{};
            std::filesystem::create_directories(dir, ec);

            std::ofstream out{dir / "l10n.json", std::ios::trunc};
            if (out) {
                out << "{\\n  \\"Language\\": \\"" << json_escape(g_current) << "\\"\\n}\\n";
            }
        } catch (...) {
        }
    }

    // -------------------------------------------------------------- selection

    const std::vector<Language>& list() {
        std::lock_guard<std::mutex> lock{g_mutex};
        if (!g_attempted_init) {
            // Callers (UI) may run before initialize(); keep them safe.
            scan_language_files();
        }
        return g_languages;
    }

    const std::string& current_code() {
        return g_current;
    }

    std::string current_name() {
        for (const auto& lang : list()) {
            if (lang.code == g_current) {
                return lang.name;
            }
        }
        return g_current;
    }

    bool set_current(const std::string& code) {
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock{g_mutex};
            if (code == "en" || find_language(code) != nullptr) {
                apply_language(code);
                ok = true;
            }
        }

        if (ok) {
            reload(); // also persists the choice
        }
        return ok;
    }

    // ---------------------------------------------------------------- helpers

    std::string export_builtin() {
        std::error_code ec{};
        const auto dir = langs_dir();
        std::filesystem::create_directories(dir, ec);

        const auto target = dir / (std::string{kBuiltinCode} + ".json");
        if (std::filesystem::exists(target, ec)) {
            return {};
        }

        std::ofstream out{target, std::ios::trunc};
        if (!out) {
            return {};
        }

        out << "{\\n";
        out << "  \\"Language\\": \\"简体中文\\",\\n";
        out << "  \\"Code\\": \\"" << kBuiltinCode << "\\",\\n";
        out << "  \\"Version\\": 1,\\n";
        out << "  \\"Entries\\": {\\n";

        bool first = true;
        for (const auto& [key, value] : kBuiltin) {
            if (!first) {
                out << ",\\n";
            }
            first = false;
            out << "    \\"" << json_escape(std::string{key}) << "\\": \\"" << json_escape(value) << "\\"";
        }

        out << "\\n  }\\n}\\n";

        return target.string();
    }

    bool enabled() {
        return g_current != "en" && (!g_external.empty() || g_current == kBuiltinCode);
    }

    void set_enabled(bool v) {
        set_current(v ? kBuiltinCode : "en");
    }
} // namespace l10n
'''

def main():
    with io.open(SRC, encoding="utf-8") as fh:
        original = fh.read()

    # Keep only the initialiser lines of the old table, verbatim.
    kept = []
    for line in original.splitlines():
        if ENTRY_RE.match(line):
            line = line.rstrip()
            if not line.endswith(","):
                line += ","
            kept.append(line)

    if not kept:
        sys.exit("no entries found in %s" % SRC)

    # Trailing comma is fine in an initialiser list.
    body = "\n".join("            " + line for line in kept)

    out = HEADER + body + "\n" + FOOTER

    backup = SRC + ".bak_i18n"
    if not os.path.exists(backup):
        with io.open(backup, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(original)

    with io.open(SRC, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(out)

    print("entries carried over: %d" % len(kept))
    print("written: %s" % SRC)
    print("backup:  %s" % backup)


if __name__ == "__main__":
    main()
