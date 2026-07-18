#!/usr/bin/env python3
"""Convert English text into ARPABET-style phonemes.

This is deliberately just the "text to phonemes" half of speaking text
with the ROBOTALK CLAP instrument (this directory) -- it knows
nothing about note numbers, tempo, or .rptp files. See phonemes2rptp.py
for turning a phoneme sequence into a poketrack pattern; that's a
separate, composable step, so you can also feed it phonemes from
somewhere else entirely (hand-written, another G2P tool, etc).

Primary source: a bundled, gzip-compressed copy of the CMU Pronouncing
Dictionary (data/cmudict.dict.gz, BSD-licensed -- see
data/CMUDICT_LICENSE.txt) for real dictionary lookups across ~135k known
English words. Falls back to a small clean-room letter-to-sound heuristic
(covering vowels, diphthongs, and consonants) for words not in the
dictionary -- typically made-up words or brand names, e.g. "poketrack"
itself, or "robotalk".

The 37 phoneme symbols produced here (IY, IH, EH, AE, AA, AH, ER, AO, UH,
UW, EY, AY, AW, OY, OW, L, R, W, Y, M, N, NG, S, Z, SH, ZH, F, V, TH, DH,
HH, P, T, K, B, D, G) match exactly what
src/phonemes.rs maps to notes -- no translation
table needed. CMUdict also has CH and JH (affricates), which robotalk
doesn't model as their own phoneme; both are real stop+fricative
combinations acoustically, so they're expanded into two symbols instead
(CH -> T, SH; JH -> D, ZH) rather than needing a plugin change.

No third-party dependencies.

Usage:
  text2phonemes.py "hello world"
  python3 -c "from text2phonemes import text_to_phonemes; print(text_to_phonemes('hi'))"
"""

import gzip
import os
import re
import sys

_DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
_DICT_PATH = os.path.join(_DATA_DIR, "cmudict.dict.gz")

_STRESS_RE = re.compile(r"[012]$")
_VARIANT_RE = re.compile(r"\(\d+\)$")

# CMUdict has these two affricates; robotalk doesn't model them as their
# own phoneme (they're acoustically just stop+fricative), so expand them.
_EXPAND = {"CH": ["T", "SH"], "JH": ["D", "ZH"]}

_dict_cache = None


def _expand(symbols):
    out = []
    for s in symbols:
        out.extend(_EXPAND.get(s, [s]))
    return out


def _load_dict():
    global _dict_cache
    if _dict_cache is not None:
        return _dict_cache
    d = {}
    with gzip.open(_DICT_PATH, "rt", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(";;;"):
                continue
            word, _, phones = line.partition(" ")
            word = _VARIANT_RE.sub("", word)
            if word in d:
                continue  # keep only the first (primary) pronunciation
            d[word] = [_STRESS_RE.sub("", p) for p in phones.split()]
    _dict_cache = d
    return d


# --- clean-room letter-to-sound fallback, for words not in CMUdict ---

# Longest patterns first. Lists are expanded inline (affricate-style
# clusters); everything else is a single ARPABET symbol.
_PATTERNS = [
    ("tch", ["T", "SH"]), ("dge", ["D", "ZH"]),
    ("oul", ["UH"]),  # could/would/should
    ("ng", ["NG"]), ("ck", ["K"]), ("ph", ["F"]), ("wh", ["W"]), ("qu", ["K", "W"]),
    ("th", ["TH"]), ("sh", ["SH"]), ("ch", ["T", "SH"]),
    ("ee", ["IY"]), ("ea", ["IY"]), ("ie", ["IY"]), ("ei", ["IY"]),
    ("oo", ["UW"]), ("ou", ["UW"]), ("ew", ["UW"]), ("ue", ["UW"]),
    ("oi", ["OY"]), ("oy", ["OY"]),
    ("ai", ["EY"]), ("ay", ["EY"]),
    ("oa", ["OW"]), ("ow", ["AW"]),
    ("au", ["AO"]), ("aw", ["AO"]),
    ("er", ["ER"]), ("ir", ["ER"]), ("ur", ["ER"]),
    ("ar", ["AA"]), ("or", ["AO"]),
    ("a", ["AE"]), ("e", ["EH"]), ("i", ["IH"]), ("o", ["AH"]), ("u", ["AH"]),
    ("b", ["B"]), ("c", ["K"]), ("d", ["D"]), ("f", ["F"]), ("g", ["G"]),
    ("h", ["HH"]), ("j", ["D", "ZH"]), ("k", ["K"]), ("l", ["L"]), ("m", ["M"]),
    ("n", ["N"]), ("p", ["P"]), ("q", ["K"]), ("r", ["R"]), ("s", ["S"]),
    ("t", ["T"]), ("v", ["V"]), ("w", ["W"]), ("x", ["K", "S"]), ("z", ["Z"]),
]

# "Magic e": vowel + single consonant + silent e signals a long
# vowel/diphthong (make, theme, time, home, use) rather than the plain
# short-vowel fallback above -- the classic VCe vs VCCe English spelling
# rule (hoping vs hopping, cuter vs cutter).
_MAGIC_E = {"a": "EY", "e": "IY", "i": "AY", "o": "OW", "u": "UW"}


def _word_to_phonemes_heuristic(word):
    phonemes = []
    i = 0
    n = len(word)
    while i < n:
        ch = word[i]
        if (ch in _MAGIC_E and i + 2 < n
                and word[i + 1] not in "aeiouy" and word[i + 2] == "e"):
            phonemes.append(_MAGIC_E[ch])
            i += 2
            if not word.startswith("er", i):
                i += 1  # silent trailing e, nothing follows to pair it with
            continue
        if ch == "y" and i + 1 < n and word[i + 1] in "aeiou":
            phonemes.append("Y")  # consonant y (yes, yellow), not vowel y
            i += 1
            continue
        matched = False
        for pattern, symbols in _PATTERNS:
            if word.startswith(pattern, i):
                phonemes.extend(symbols)
                i += len(pattern)
                matched = True
                break
        if not matched:
            i += 1  # unrecognized character: skip
    return phonemes


def word_to_phonemes(word):
    """Return the ARPABET phoneme list for one word (dictionary first,
    clean-room heuristic fallback for anything not found)."""
    clean = re.sub(r"[^a-z']", "", word.lower())
    if not clean:
        return []
    d = _load_dict()
    if clean in d:
        return _expand(d[clean])
    bare = re.sub(r"[^a-z]", "", clean)
    if bare in d:
        return _expand(d[bare])
    return _word_to_phonemes_heuristic(bare)


def text_to_phonemes(text):
    """Return a list of (word, [phonemes]) pairs for each word in `text`."""
    return [(word, word_to_phonemes(word)) for word in text.split()]


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv:
        print("usage: text2phonemes.py TEXT", file=sys.stderr)
        return 1
    for word, phonemes in text_to_phonemes(" ".join(argv)):
        print("%-20s %s" % (word, " ".join(phonemes)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
