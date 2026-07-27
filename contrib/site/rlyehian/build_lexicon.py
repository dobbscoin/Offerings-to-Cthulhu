#!/usr/bin/env python3
"""Build lexicon.js for the 23skidoo.info R'lyehian translator.

Merges three vocabularies (see research/rlyehian-translation-sources.md):
  1. Fan glossary  — research/rlyehian-fan-glossary.tsv (alt.horror.cthulhu /
     yog-sothoth lineage, "R'lyehian as a Toy Language"). Multi-sense,
     authoritative for *meaning*.
  2. LingoJam tables — research/rlyehian-lingojam-{words,phrases}.tsv.
     Authoritative for what the community's usual decoder produces.
  3. Chain lexicon — the 32 words of src/miner.cpp::RlyehianVerse, the only
     words the chain itself speaks during Phase B Dreaming. Badge only.

Plus a small curated table for canon compounds (ph'nglui, mglw'nafh, ...)
whose derivations follow the fan glossary's own morphology.

Usage:  python3 build_lexicon.py            (writes lexicon.js next to itself)
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.normpath(os.path.join(HERE, "..", "..", "..", "research"))
WORDS_TSV = os.path.join(RESEARCH, "rlyehian-lingojam-words.tsv")
PHRASES_TSV = os.path.join(RESEARCH, "rlyehian-lingojam-phrases.tsv")
GLOSSARY_TSV = os.path.join(RESEARCH, "rlyehian-fan-glossary.tsv")

# The 32 words the chain speaks — src/miner.cpp::RlyehianVerse, byte-exact.
CHAIN_LEXICON = [
    "ph'nglui", "mglw'nafh", "Cthulhu", "R'lyeh", "wgah'nagl", "fhtagn",
    "ya", "nafl", "hupadgh", "n'gha", "k'yarnak", "ngah", "gof'nn", "syha'h",
    "gnaiih", "ftaghu", "ehye", "lloig", "ilyaa", "ron", "throd", "uaaah",
    "ooboshu", "vulgtlagln", "ya-na-kadishtu", "ep", "goka", "ah", "ee",
    "nog", "kadishtu", "ng",
]

# Curated entries: canon compounds + proper names the fan glossary keeps as
# morphology exercises. Derivations follow the glossary's own affix table.
# type "derived" is displayed as such — these are reconstructions, not canon
# dictionary rows. Tuple: (type, short primary gloss, full meaning).
CURATED = {
    "ph'nglui":       ("derived", "beyond the threshold", "beyond the threshold (ph'- 'over/beyond' + nglui 'threshold')"),
    "mglw'nafh":      ("derived", "dead-yet-dreaming", "dead-yet-dreaming (mg 'yet' + lw'nafh 'dream / transmit')"),
    "wgah'nagl":      ("derived", "dwelling-place", "dwelling-place (wgah'n 'reside in' + -agl 'place')"),
    "ngah":           ("derived", "and acts", "and acts (ng- 'and/then' + ah 'do')"),
    "ya-na-kadishtu": ("derived", "the unknowable", "I do not understand — the unknowable (ya + na- + kadishtu)"),
    "cthulhu":        ("proper", "Cthulhu", "Cthulhu, the Great Old One, dead-yet-dreaming beneath the sea"),
    "r'lyeh":         ("proper", "R'lyeh", "R'lyeh, the sunken city where dead Cthulhu waits dreaming"),
    "hastur":         ("proper", "Hastur", "Hastur, the Unspeakable, King in Yellow"),
    "yog-sothoth":    ("proper", "Yog-Sothoth", "Yog-Sothoth, the Gate and the Key"),
    "shub-niggurath": ("proper", "Shub-Niggurath", "Shub-Niggurath, the Black Goat of the Woods"),
    "azathoth":       ("proper", "Azathoth", "Azathoth, the blind idiot god of nuclear chaos"),
    "nyarlathotep":   ("proper", "Nyarlathotep", "Nyarlathotep, the Crawling Chaos"),
    "yuggoth":        ("proper", "Yuggoth", "Yuggoth, the dark world at the rim (Pluto)"),
    "ia":             ("derived", "hail! / glory!", "hail! / glory! (ritual exclamation, Derleth-era usage)"),
}


def primary_gloss(meaning):
    """Short display gloss: first sense, parens stripped. 'wait / sleep' ->
    'wait'; '(finish spell)' -> 'finish spell'."""
    first = re.split(r"[/;,]", meaning)[0].strip()
    bare = re.sub(r"\(.*?\)", "", first).strip()   # drop annotations
    if not bare:
        bare = re.sub(r"[()]", "", first).strip()  # fully-parenthesized gloss
    return bare or meaning.strip()

TEMPLATE_RE = re.compile(r"\{\{[a-z]+\}\}")


def norm(s):
    s = s.replace("’", "'").replace("‘", "'").replace("`", "'")
    return s.strip()


def strip_templates(s):
    return TEMPLATE_RE.sub("", s).strip()


def load_tsv(path, skip_comments=False):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or (skip_comments and line.startswith("#")):
                continue
            parts = line.split("\t")
            if len(parts) >= 2:
                rows.append([norm(p) for p in parts])
    return rows


def main():
    words = load_tsv(WORDS_TSV)
    phrases = load_tsv(PHRASES_TSV)
    glossary = load_tsv(GLOSSARY_TSV, skip_comments=True)

    # --- encode: english -> rlyehian, first-in-file wins (LingoJam order) ---
    # Templates stripped on both sides: "is{{verb}}" -> "is", "ah{{verb}}" -> "ah".
    # The regex/template layer of LingoJam is unverified (sources doc §4), so we
    # only ever emit whole-token forms.
    enc_words = {}
    for eng, rly in words:
        k = strip_templates(eng).lower()
        v = strip_templates(rly)
        if k and v and k not in enc_words:
            enc_words[k] = v
    enc_phrases = {}
    for eng, rly in phrases:
        k = strip_templates(eng).lower()
        v = strip_templates(rly)
        if k and v and k not in enc_phrases:
            enc_phrases[k] = v
    # Multiword keys from the words file behave like phrases at match time.
    for k, v in list(enc_words.items()):
        if " " in k:
            enc_phrases.setdefault(k, v)
            del enc_words[k]

    # --- decode: rlyehian -> [english senses, file order] -----------------
    dec_words = {}
    for eng, rly in words:
        k = strip_templates(rly).lower()
        v = strip_templates(eng)
        if not k or not v:
            continue
        dec_words.setdefault(k, [])
        if v not in dec_words[k]:
            dec_words[k].append(v)
    dec_phrases = {}
    for eng, rly in phrases:
        k = strip_templates(rly).lower()
        v = strip_templates(eng)
        if not k or not v:
            continue
        dec_phrases.setdefault(k, [])
        if v not in dec_phrases[k]:
            dec_phrases[k].append(v)
    for k, v in list(dec_words.items()):
        if " " in k:
            for s in v:
                dec_phrases.setdefault(k, [])
                if s not in dec_phrases[k]:
                    dec_phrases[k].append(s)
            del dec_words[k]

    # --- fan glossary ------------------------------------------------------
    gloss = {}
    for row in glossary:
        term, typ, meaning = row[0], row[1], row[2]
        key = term.strip("-").lower()  # affixes keyed bare; type keeps the role
        gloss[key] = {"t": typ, "m": meaning, "p": primary_gloss(meaning), "term": term}
    for key, (typ, primary, meaning) in CURATED.items():
        if key not in gloss:
            gloss[key] = {"t": typ, "m": meaning, "p": primary, "term": key}
    # Words carrying a leading apostrophe ('ai, 'bthnk, 'fhalma) also match
    # bare — the tokenizer treats a leading apostrophe as punctuation.
    for key in list(gloss.keys()):
        if key.startswith("'") and key[1:] not in gloss:
            gloss[key[1:]] = gloss[key]

    chain = sorted({w.lower() for w in CHAIN_LEXICON})

    lex = {
        "chain": chain,
        "chainDisplay": CHAIN_LEXICON,
        "gloss": gloss,
        "encWords": enc_words,
        "encPhrases": enc_phrases,
        "decWords": dec_words,
        "decPhrases": dec_phrases,
    }
    out = os.path.join(HERE, "lexicon.js")
    with open(out, "w", encoding="utf-8") as f:
        f.write("// GENERATED by build_lexicon.py — do not edit by hand.\n")
        f.write("// Sources: rlyehian-fan-glossary.tsv, rlyehian-lingojam-{words,phrases}.tsv,\n")
        f.write("// src/miner.cpp::RlyehianVerse. See research/rlyehian-translation-sources.md.\n")
        f.write("const LEX = ")
        f.write(json.dumps(lex, ensure_ascii=False, separators=(",", ":")))
        f.write(";\n")
    print(f"wrote {out}")
    print(f"  encode: {len(enc_words)} words, {len(enc_phrases)} phrases")
    print(f"  decode: {len(dec_words)} forms, {len(dec_phrases)} phrases")
    print(f"  glossary: {len(gloss)} entries; chain lexicon: {len(chain)} words")


if __name__ == "__main__":
    main()
