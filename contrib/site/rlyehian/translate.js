// R'lyehian translator engine — 23skidoo.info
// Truthfulness rules (see research/rlyehian-translation-sources.md):
//  - phrase matches before words, longest first (LingoJam-compatible)
//  - decode shows ALL senses, not first-gloss-wins; fan glossary outranks LingoJam
//  - unknown words pass through visibly marked, never silently mangled
//  - affix analysis is offered as a labeled guess, never as fact
// Requires lexicon.js (const LEX) loaded first. Node-testable via module.exports.

(function (global) {
  "use strict";

  var LEXICON = typeof LEX !== "undefined" ? LEX : null;
  function lex() {
    if (!LEXICON) throw new Error("lexicon.js not loaded");
    return LEXICON;
  }

  var CHAIN = null;
  function chainSet() {
    if (!CHAIN) {
      CHAIN = {};
      lex().chain.forEach(function (w) { CHAIN[w] = true; });
    }
    return CHAIN;
  }

  // Affix table — fan glossary morphology. Every hit is a GUESS ("morph").
  var PREFIXES = [
    ["mgep", "past-marker"],
    ["ngep", "past-marker"],
    ["nafl", "not / not-present"],
    ["nnn", "watch / protect"],
    ["ng", "and / then"],
    ["na", "not"],
    ["mg", "yet / opposite-of"],
    ["ph'", "over / beyond"],
    ["f'", "they / their"],
    ["h'", "it / its"],
    ["c", "we / our"],
    ["y", "I / my"]
  ];
  var SUFFIXES = [
    ["nglui", "gate to"],
    ["'drn", "one who"],
    ["nyth", "servant of"],
    ["agl", "place of"],
    ["oth", "native of"],
    ["yar", "time of"],
    ["or", "aspect of"],
    ["og", "(emphatic)"]
  ];

  function norm(s) {
    return s.replace(/[‘’ʼ`]/g, "'");
  }

  // Tokenize into alternating {w, sep}. Words keep internal '/-; edge
  // punctuation lands in sep.
  function tokenize(text) {
    var toks = [];
    var re = /[A-Za-z][A-Za-z'\-]*[A-Za-z']|[A-Za-z]/g;
    var last = 0, m;
    text = norm(text);
    while ((m = re.exec(text)) !== null) {
      toks.push({ sep: text.slice(last, m.index), w: m[0] });
      last = m.index + m[0].length;
    }
    return { toks: toks, tail: text.slice(last) };
  }

  function phraseKey(words) {
    return words.join(" ").toLowerCase();
  }

  function buildPhraseIndex(table) {
    // table: key -> value; keys are multiword strings. Index by token count.
    var idx = { max: 1, map: {} };
    Object.keys(table).forEach(function (k) {
      var nk = norm(k).toLowerCase().replace(/[!.,;:?]/g, "").replace(/\s+/g, " ").trim();
      var n = nk.split(" ").length;
      if (n > idx.max) idx.max = n;
      if (!(nk in idx.map)) idx.map[nk] = table[k];
    });
    return idx;
  }

  var encIdx = null, decIdx = null;
  function encPhraseIdx() { return encIdx || (encIdx = buildPhraseIndex(lex().encPhrases)); }
  function decPhraseIdx() { return decIdx || (decIdx = buildPhraseIndex(lex().decPhrases)); }

  function firstSense(s) {
    // "wait / sleep" -> "wait"; "after; with hai, later / then" -> "after"
    return s.split(/[/;]/)[0].replace(/\(.*?\)/g, "").trim() || s.trim();
  }

  // ---- decode: R'lyehian -> English ------------------------------------
  // Returns { text, segments: [...] }
  function lookupWord(w) {
    var L = lex();
    var g = L.gloss[w];
    var lj = L.decWords[w] || null;
    if (!g && !lj) return null;
    return {
      gloss: g || null,
      lj: lj,
      chain: !!chainSet()[w]
    };
  }

  function analyzeMorph(w) {
    var i, p, s, rest, hit;
    // plural: doubled final consonant
    if (w.length > 2 && w[w.length - 1] === w[w.length - 2] && !/[aeiou']/.test(w[w.length - 1])) {
      hit = lookupWord(w.slice(0, -1));
      if (hit) return { stem: w.slice(0, -1), hit: hit, note: "plural (doubled final consonant)" };
    }
    for (i = 0; i < PREFIXES.length; i++) {
      p = PREFIXES[i];
      if (w.length > p[0].length + 1 && w.indexOf(p[0]) === 0) {
        rest = w.slice(p[0].length);
        hit = lookupWord(rest);
        if (hit) return { stem: rest, hit: hit, note: p[0] + "- (" + p[1] + ") + stem" };
      }
    }
    for (i = 0; i < SUFFIXES.length; i++) {
      s = SUFFIXES[i];
      if (w.length > s[0].length + 1 && w.lastIndexOf(s[0]) === w.length - s[0].length) {
        rest = w.slice(0, w.length - s[0].length);
        hit = lookupWord(rest);
        if (hit) return { stem: rest, hit: hit, note: "stem + -" + s[0] + " (" + s[1] + ")" };
      }
    }
    return null;
  }

  function decode(text) {
    var t = tokenize(text);
    var idx = decPhraseIdx();
    var segs = [];
    var out = "";
    var i = 0, n, key, val, j, words, seg;
    while (i < t.toks.length) {
      var matched = false;
      for (n = Math.min(idx.max, t.toks.length - i); n >= 2; n--) {
        words = [];
        for (j = 0; j < n; j++) words.push(t.toks[i + j].w.toLowerCase());
        key = phraseKey(words);
        val = idx.map[key];
        if (val !== undefined) {
          seg = {
            kind: "phrase", src: srcSlice(t.toks, i, n),
            out: val[0], senses: val.slice(),
            prov: "LJ-phrase", chain: false, note: "whole-phrase idiom"
          };
          out += t.toks[i].sep + seg.out;
          segs.push(seg);
          i += n; matched = true;
          break;
        }
      }
      if (matched) continue;
      var tok = t.toks[i];
      var w = tok.w.toLowerCase();
      var hit = lookupWord(w);
      if (hit) {
        seg = wordSeg(tok.w, hit, null);
      } else {
        var mo = analyzeMorph(w);
        if (mo) {
          seg = wordSeg(tok.w, mo.hit, mo.note);
          seg.out = composeMorph(seg.out, mo);
        } else {
          seg = { kind: "unk", src: tok.w, out: tok.w, senses: [],
                  prov: "unk", chain: !!chainSet()[w], note: "not in any source" };
        }
      }
      out += tok.sep + seg.out;
      segs.push(seg);
      i++;
    }
    out += t.tail;
    return { text: cap(out), segments: segs };
  }

  // Turn a stem gloss + morphology note into readable English.
  function composeMorph(stemGloss, mo) {
    var note = mo.note;
    if (note.indexOf("plural") === 0) return pluralize(stemGloss);
    var m = note.match(/^(\S+)- \((.+)\) \+ stem$/);
    if (m) {
      var pm = m[2].split("/")[0].trim();
      if (pm === "past-marker") return "did-" + stemGloss;
      return pm + "-" + stemGloss;
    }
    m = note.match(/^stem \+ -(\S+) \((.+)\)$/);
    if (m) {
      var sm = m[2];
      if (sm === "(emphatic)") return stemGloss + "!";
      return sm + " " + stemGloss;
    }
    return stemGloss;
  }

  function pluralize(s) {
    if (/(child|children|people|folk)$/i.test(s)) return s;
    if (/[sxz]$/.test(s) || /[cs]h$/.test(s)) return s + "es";
    if (/[^aeiou]y$/.test(s)) return s.slice(0, -1) + "ies";
    return s + "s";
  }

  function wordSeg(src, hit, morphNote) {
    var senses = [];
    var prov, primary;
    if (hit.gloss) {
      primary = hit.gloss.p || firstSense(hit.gloss.m);
      senses.push({ s: hit.gloss.m, from: hit.gloss.t === "proper" ? "canon" :
                    (hit.gloss.t === "derived" ? "derived" : "glossary") });
      prov = hit.gloss.t === "word" || hit.gloss.t === "prefix" ||
             hit.gloss.t === "suffix" || hit.gloss.t === "conjunction" ? "G" :
             (hit.gloss.t === "proper" ? "canon" : "D");
    }
    if (hit.lj) {
      if (!primary) primary = hit.lj[0];
      senses.push({ s: hit.lj.join(", "), from: "lingojam" });
      if (!prov) prov = "LJ";
    }
    var seg = { kind: morphNote ? "morph" : "word", src: src, out: primary,
                senses: senses, prov: prov, chain: hit.chain,
                note: morphNote || null };
    return seg;
  }

  function srcSlice(toks, i, n) {
    var s = toks[i].w;
    for (var j = 1; j < n; j++) s += toks[i + j].sep + toks[i + j].w;
    return s;
  }

  // ---- encode: English -> R'lyehian ------------------------------------
  function encode(text) {
    var t = tokenize(text);
    var idx = encPhraseIdx();
    var words = lex().encWords;
    var segs = [];
    var out = "";
    var i = 0, n, j, key, val, seg, ws;
    while (i < t.toks.length) {
      var matched = false;
      for (n = Math.min(idx.max, t.toks.length - i); n >= 2; n--) {
        ws = [];
        for (j = 0; j < n; j++) ws.push(t.toks[i + j].w.toLowerCase());
        key = phraseKey(ws);
        val = idx.map[key];
        if (val !== undefined) {
          seg = { kind: "phrase", src: srcSlice(t.toks, i, n), out: val,
                  prov: "LJ-phrase", chain: false, senses: [], note: "whole-phrase idiom" };
          out += t.toks[i].sep + seg.out;
          segs.push(seg);
          i += n; matched = true;
          break;
        }
      }
      if (matched) continue;
      var tok = t.toks[i];
      var w = tok.w.toLowerCase();
      val = words[w];
      if (val === undefined) {
        seg = { kind: "unk", src: tok.w, out: tok.w, prov: "unk", chain: false,
                senses: [], note: "no R'lyehian word — left untranslated" };
      } else {
        seg = { kind: "word", src: tok.w, out: val, prov: "LJ", senses: [],
                chain: !!chainSet()[val.toLowerCase()], note: null };
      }
      out += tok.sep + seg.out;
      segs.push(seg);
      i++;
    }
    out += t.tail;
    return { text: cap(out), segments: segs };
  }

  function cap(s) {
    var m = s.match(/[a-z]/i);
    if (m && m[0] >= "a" && m[0] <= "z") {
      var i = s.indexOf(m[0]);
      s = s.slice(0, i) + m[0].toUpperCase() + s.slice(i + 1);
    }
    return s;
  }

  var api = { encode: encode, decode: decode, tokenize: tokenize,
              setLexicon: function (l) { LEXICON = l; CHAIN = null; encIdx = null; decIdx = null; } };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  global.Rlyehian = api;
})(typeof window !== "undefined" ? window : globalThis);
