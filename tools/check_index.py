"""Fetch the live index and run the parse/classify/filter rules over it.

A reference implementation of src/update/manifest.cpp and src/update/plan.cpp, kept
deliberately close to the C++ so a disagreement between the two is a real bug in one of
them. Run it after any change to line parsing, category classification, or the
applicability filter.

    py tools/check_index.py                 warframe, public branch, english
    py tools/check_index.py --title soulframe
    py tools/check_index.py --lang de --dx12
    py tools/check_index.py --save index.txt
"""

import argparse
import collections
import json
import lzma
import os
import random
import re
import struct
import sys
import urllib.request

# mirrors originHosts in src/update/plan.cpp
ORIGIN = {
    "warframe": {"public": "https://origin.warframe.com",
                 "test": "https://origin-test.warframe.com",
                 "dev": "https://origin-dev.warframe.com"},
    "soulframe": {"public": "https://origin.soulframe.com",
                  "test": "https://origin-test.soulframe.com",
                  "dev": "https://origin-dev.soulframe.com"},
}

BRANCHES = ["public", "test", "dev"]

TOOL_PREFIXES = ["/Tools/Launcher.exe", "/Tools/RemoteCrashSender.exe",
                 "/Tools/Windows/x64/dbghelp.dll", "/Tools/Windows/x64/symsrv.dll",
                 "/Tools/Windows/x64/EOSSDK", "/Tools/CEF3_1/"]

LOCALIZED_EXTENSIONS = [".cache", ".toc", ".rtf"]

SKIP_DEFAULTS = ["Tools\\Windows\\x64\\discord_game_sdk.dll"]

HASH = re.compile(r"[0-9a-fA-F]{32}")


def normalise_skip(path):
    return path.strip().replace("/", "\\").lstrip("\\").lower()


def load_skip(path):
    skips = set(normalise_skip(p) for p in SKIP_DEFAULTS)
    cfg = os.path.join(os.path.dirname(path), "launcher.json")
    if os.path.exists(cfg):
        try:
            with open(cfg, "r", encoding="utf-8-sig") as handle:
                data = json.load(handle)
            # "skip" was the original spelling; read it so an early file still loads
            for entry in data.get("exclude", data.get("skip", [])):
                skips.add(normalise_skip(entry))
        except (OSError, ValueError):
            pass
        return skips
    # legacy skip.txt fallback keeps the leading-'-' removal affordance
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8-sig") as handle:
            for line in handle:
                entry = normalise_skip(line)
                if not entry or entry.startswith("#"):
                    continue
                if entry.startswith("-"):
                    skips.discard(normalise_skip(entry[1:]))
                else:
                    skips.add(entry)
    return skips


def fetch_index(title, branch):
    url = "%s/origin/%08X/index.txt.lzma" % (ORIGIN[title][branch], random.getrandbits(32))
    with urllib.request.urlopen(url, timeout=60) as response:
        blob = response.read()
    props, dict_size, raw_size = blob[0], struct.unpack("<I", blob[1:5])[0], struct.unpack("<Q", blob[5:13])[0]
    lc, rest = props % 9, props // 9
    body = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(blob)
    meta = dict(url=url, compressed=len(blob), decompressed=len(body),
                lc=lc, lp=rest % 5, pb=rest // 5, dict_size=dict_size, declared=raw_size)
    return body, meta


def parse_line(line):
    compression, marker = "lzma", line.find(".lzma,")
    if marker == -1:
        compression, marker = "bulk", line.find(".bulk,")
    if marker in (-1, 0):
        return "NoExtension", None
    dot = line.rfind(".", 0, marker)
    if dot == -1 or marker != dot + 33:
        return "HashNotWhereExpected", None
    digest = line[dot + 1:dot + 33]
    if not HASH.fullmatch(digest):
        return "BadHash", None
    size_text = line[marker + 6:]
    if not size_text.isdigit():
        return "BadSize", None
    install = line[:dot].replace("/", "\\").lstrip("\\")
    if not install or ":" in install or any(s in ("", ".", "..") for s in install.split("\\")):
        return "UnsafePath", None
    return None, dict(url=line[:marker + 5], install=install, hash=digest.upper(),
                      size=int(size_text), compression=compression)


def classify(url, install):
    low = url.lower()
    if "/cef" in low:
        return "Cef"
    if any(low.startswith(p.lower()) for p in TOOL_PREFIXES) or "/steam" in low:
        return "Tools"
    if "\\" not in install and install.lower().endswith(".exe"):
        return "MainExe"
    if install.lower().endswith((".cache", ".toc")):
        return "CacheOrToc"
    return "Data"


def language_allows(install, language):
    dot = install.rfind(".")
    extension = install[dot:] if dot != -1 else ""
    if extension.lower() not in LOCALIZED_EXTENSIONS:
        return True
    stem = install[:len(install) - len(extension)]
    if len(stem) <= 3 or stem[-3] != "_":
        return True
    code = stem[-2:].lower()
    if code in ("xx", language.lower()):
        return True
    if "misc" not in stem.lower():
        return False
    return code == "en"


def applies(entry, args):
    low = entry["url"].lower()
    if not args.steam and "/steam" in low:
        return False
    if not args.eos and "/eossdk" in low:
        return False
    if args.branch == "public" and "d3d12sdklayers" in low:
        return False
    if not args.dx12 and ("dx12.toc" in low or "dx12.cache" in low):
        return False
    return language_allows(entry["install"], args.lang)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", choices=sorted(ORIGIN), default="warframe")
    parser.add_argument("--branch", choices=BRANCHES, default="public")
    parser.add_argument("--lang", default="en")
    parser.add_argument("--steam", action="store_true")
    parser.add_argument("--eos", action="store_true")
    parser.add_argument("--dx12", action="store_true")
    parser.add_argument("--save", metavar="PATH")
    parser.add_argument("--skip-file", default="skip.txt")
    args = parser.parse_args()

    body, meta = fetch_index(args.title, args.branch)
    if args.save:
        open(args.save, "wb").write(body)
    print("%s\n  %d compressed, %d decompressed (declared %d), lc=%d lp=%d pb=%d dict=%d"
          % (meta["url"], meta["compressed"], meta["decompressed"], meta["declared"],
             meta["lc"], meta["lp"], meta["pb"], meta["dict_size"]))
    if meta["decompressed"] != meta["declared"]:
        print("  [!] declared size does not match")

    lines = [l.strip() for l in body.decode("utf-8").split("\n") if l.strip()]
    errors, entries = collections.Counter(), []
    for line in lines:
        error, entry = parse_line(line)
        if error:
            errors[error] += 1
            print("  [!] %s: %s" % (error, line))
        else:
            entry["category"] = classify(entry["url"], entry["install"])
            entries.append(entry)

    print("\n%d lines, %d parsed, %d rejected" % (len(lines), len(entries), sum(errors.values())))
    print("categories: %s" % dict(collections.Counter(e["category"] for e in entries)))

    skip = load_skip(args.skip_file)
    applicable = [e for e in entries if applies(e, args)]
    filtered = len(entries) - len(applicable)
    kept = [e for e in applicable if normalise_skip(e["install"]) not in skip]
    skipped = len(applicable) - len(kept)
    total = sum(e["size"] for e in kept)
    print("lang=%s dx12=%s: %d of %d entries, %.2f GiB to download from empty "
          "(%d filtered, %d skipped)"
          % (args.lang, args.dx12, len(kept), len(entries), total / 2 ** 30, filtered, skipped))

    partners = collections.defaultdict(set)
    for entry in entries:
        if entry["category"] == "CacheOrToc":
            stem, _, extension = entry["install"].rpartition(".")
            partners[stem].add(extension)
    orphans = {k: sorted(v) for k, v in partners.items() if v != {"cache", "toc"}}
    print("cache/toc stems missing a partner: %s" % (orphans or "none"))

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
