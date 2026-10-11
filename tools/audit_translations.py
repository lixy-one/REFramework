"""Audit l10n::tr() call sites against the translation table.

Walks every .cpp/.hpp under src/ and shared/, extracts the string literal
passed to l10n::tr(...), and reports which ones have no entry in the built-in
table (src/Localization.cpp) or in reframework/langs/zh-CN.json.

Usage:
    python tools/audit_translations.py [--json <path-to-lang-json>]

Exit code is 0 either way; the report is meant for humans.
"""

import argparse
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))

# l10n::tr("...") with support for escaped quotes inside the literal.
TR_CALL_RE = re.compile(r'l10n::tr\(\s*"((?:[^"\\]|\\.)*)"', re.MULTILINE)

# Skips: we don't want the translation table itself, nor third-party trees.
SKIP_DIRS = {"build64_vs2026", "dependencies", ".git", "build"}


def unescape_cpp(text):
    """C++ literal -> real string (only the escapes that appear in practice)."""
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            n = text[i + 1]
            mapping = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "0": "\0"}
            if n in mapping:
                out.append(mapping[n])
                i += 2
                continue
        out.append(c)
        i += 1
    return "".join(out)


def iter_sources():
    for base in ("src", "shared"):
        base_path = os.path.join(ROOT, base)
        if not os.path.isdir(base_path):
            continue
        for dirpath, dirnames, filenames in os.walk(base_path):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in filenames:
                if name.endswith((".cpp", ".hpp")):
                    yield os.path.join(dirpath, name)


def load_table(path):
    if not os.path.isfile(path):
        return {}
    with io.open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    return data.get("Entries", {})


def load_builtin():
    """Keys present in the compiled-in table (Localization.cpp)."""
    src = os.path.join(ROOT, "src", "Localization.cpp")
    if not os.path.isfile(src):
        return set()
    entry_re = re.compile(r'^\s*\{\s*"((?:[^"\\]|\\.)*)"\s*,', re.MULTILINE)
    with io.open(src, encoding="utf-8") as fh:
        return {unescape_cpp(m.group(1)) for m in entry_re.finditer(fh.read())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default=os.path.join(ROOT, "src", "Localization_langs", "zh-CN.json"))
    ap.add_argument("--limit", type=int, default=200)
    args = ap.parse_args()

    external = load_table(args.json)
    builtin = load_builtin()

    known = set(external.keys()) | builtin

    missing = {}     # string -> list of "file:line"
    total = 0

    for path in iter_sources():
        try:
            with io.open(path, encoding="utf-8") as fh:
                content = fh.read()
        except (OSError, UnicodeDecodeError):
            continue

        for m in TR_CALL_RE.finditer(content):
            raw = m.group(1)
            if not raw:
                continue
            text = unescape_cpp(raw)
            total += 1

            if text in known:
                continue

            line_no = content.count("\n", 0, m.start()) + 1
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            missing.setdefault(text, []).append("%s:%d" % (rel, line_no))

    print("tr() call sites scanned: %d" % total)
    print("unique strings:          %d" % len(set()))
    print("translated (built-in+json): %d" % len(known))
    print("MISSING:                 %d unique" % len(missing))
    print()

    if not missing:
        print("Nothing missing - every l10n::tr() string has a translation.")
        return

    for i, (text, where) in enumerate(sorted(missing.items(), key=lambda kv: kv[1][0])):
        if i >= args.limit:
            print("... (%d more)" % (len(missing) - args.limit))
            break
        shown = text if len(text) <= 90 else text[:87] + "..."
        print('  [%d] "%s"' % (i + 1, shown))
        print("      %s" % ", ".join(where[:4]))

    print()
    print("Add the missing keys to src/Localization_langs/zh-CN.json (and to the")
    print("built-in table in src/Localization.cpp if they should ship by default).")


if __name__ == "__main__":
    main()
