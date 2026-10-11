"""Extract the C++ translation table from src/Localization.cpp and emit
reframework/langs/zh-CN.json.

Usage:
    python tools/gen_lang_json.py [--output <path>]

The C++ table is a flat list of { "English", "Translated" } pairs. Duplicate
English keys are collapsed the same way the C++ `std::unordered_map` does
(last occurrence wins), so the generated JSON is a faithful representation.
"""

import argparse
import io
import json
import os
import re
import sys

DEFAULT_SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "..", "src", "Localization.cpp")
DEFAULT_OUTPUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "..", "src", "Localization_langs", "zh-CN.json")

# Matches:  { "English", "Translated" },   (with optional trailing comma)
ENTRY_RE = re.compile(r'^\s*\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,?\s*$')
# Matches section banners:  // ---- Camera ----
SECTION_RE = re.compile(r'^\s*//\s*-{2,}\s*(.+?)\s*-{2,}\s*$')


def unescape(text):
    """Turn C++ string-escapes into real characters."""
    return text.replace('\\\\', '\\').replace('\\"', '"')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=DEFAULT_SOURCE)
    ap.add_argument("--output", default=DEFAULT_OUTPUT)
    ap.add_argument("--lang-name", default="简体中文")
    ap.add_argument("--lang-code", default="zh-CN")
    args = ap.parse_args()

    source = os.path.normpath(args.source)
    output = os.path.normpath(args.output)

    if not os.path.isfile(source):
        sys.exit("source not found: %s" % source)

    entries = {}      # english -> translation
    order = []        # insertion order of first appearance
    sections = {}     # english -> section banner
    duplicates = 0
    sections_seen = []
    current_section = ""

    with io.open(source, encoding="utf-8") as fh:
        for line in fh:
            sec = SECTION_RE.match(line)
            if sec:
                current_section = sec.group(1)
                if current_section not in sections_seen:
                    sections_seen.append(current_section)
                continue

            m = ENTRY_RE.match(line)
            if not m:
                continue

            english = m.group(1)
            translated = m.group(2)
            if not english:
                continue

            if english in entries:
                duplicates += 1
            else:
                order.append(english)

            entries[english] = unescape(translated)
            sections[english] = current_section

    # Group by section, preserving first-seen section order, so translators
    # get a file that reads in the same order as the C++ table.
    ordered_keys = []
    for section in sections_seen:
        for key in order:
            if sections.get(key) == section:
                ordered_keys.append(key)
    for key in order:                       # any keys before the first banner
        if key not in ordered_keys:
            ordered_keys.append(key)

    payload = {
        "Language": args.lang_name,
        "Code": args.lang_code,
        "Version": 1,
        "Entries": {key: entries[key] for key in ordered_keys},
    }

    out_dir = os.path.dirname(output)
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    with io.open(output, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(payload, fh, ensure_ascii=False, indent=2)
        fh.write("\n")

    print("entries:      %d" % len(payload["Entries"]))
    print("duplicates:   %d (collapsed, last wins)" % duplicates)
    print("sections:     %d" % len(sections_seen))
    print("written:      %s" % output)


if __name__ == "__main__":
    main()
