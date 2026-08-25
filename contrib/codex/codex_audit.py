#!/usr/bin/env python3
"""Audit on-chain Codex inscriptions against the canonical corpus.

This daemon doesn't have txindex, so getrawtransaction doesn't work for
historical coinbases. Workaround: pull the raw block hex via `getblock
<hash> false` and parse the coinbase scriptSig from the binary directly.
"""
import json
import subprocess
import time
from pathlib import Path

CLI = "/usr/local/bin/Offerings-cli"   # Phase B 2026-08-16: installed artifact, not the git tree
DDIR = "-datadir=/var/lib/offeringsd"   # Phase B: btcbob no longer has a default datadir
CORPUS_PATH = Path("/home/btcbob/claude/offerings-master/contrib/codex/codex_corpus.txt")
CHUNK = 48
CODEX_DESCENT_START = 999_991
CODEX_ANCHOR = 1_000_001
CODEX_MILESTONE = 0xFFFFFFFF
CODEX_DREAMING  = 0xFFFFFFFE

DESCENT = {
    999991: "The angles turn wrong in the deep. He stirs.",
    999992: "Pressure mounts; the black seas blacken further.",
    999993: "The Conclave gathers, candles guttering green.",
    999994: "R'lyeh's drowned spires breach the surface.",
    999995: "That is not dead which can eternal lie,",
    999996: "and with strange aeons even death may die.",
    999997: "The stars come right.",
    999998: "Ph'nglui mglw'nafh Cthulhu R'lyeh wgah'nagl fhtagn.",
    999999: "He dreams no longer.",
    1000000: "IA! IA! CTHULHU FHTAGN! Block 1000000: the Restoration is come.",
}

def cli(*args):
    r = subprocess.run([CLI, DDIR, *args], capture_output=True, text=True, timeout=30)
    if r.returncode != 0:
        raise RuntimeError(f"cli failed: {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout.strip()


def read_varint(buf: bytes, pos: int):
    n = buf[pos]
    if n < 0xfd:
        return n, pos + 1
    if n == 0xfd:
        return int.from_bytes(buf[pos+1:pos+3], "little"), pos + 3
    if n == 0xfe:
        return int.from_bytes(buf[pos+1:pos+5], "little"), pos + 5
    return int.from_bytes(buf[pos+1:pos+9], "little"), pos + 9


def coinbase_scriptsig(raw: bytes) -> bytes:
    """Pull vin[0].scriptSig out of a raw block (coinbase is tx[0])."""
    pos = 80                          # skip header
    _, pos = read_varint(raw, pos)    # tx count
    pos += 4                          # tx version
    vin_count, pos = read_varint(raw, pos)
    assert vin_count == 1, f"coinbase has {vin_count} inputs"
    pos += 32 + 4                     # null prevout + 0xffffffff vout
    script_len, pos = read_varint(raw, pos)
    return raw[pos : pos + script_len]


def find_off1(scriptsig: bytes):
    """The codex egg is pushed as a single CScript data-push: OP_PUSHBYTES_N then
    N bytes of "OFF1" + idx(LE u32) + text. Read the push length from the byte
    immediately before the OFF1 magic so the text doesn't bleed into the
    COINBASE_FLAGS tag that follows (e.g. /Miningcore/ or /P2SH/)."""
    pos = scriptsig.find(b"OFF1")
    if pos < 1 or pos + 8 > len(scriptsig):
        return None
    push_len = scriptsig[pos - 1]
    if push_len < 8 or pos + push_len > len(scriptsig) + 4:  # +4 because pos points AT OFF1, not start
        return None
    text_len = push_len - 4 - 4   # minus magic, minus idx
    idx = int.from_bytes(scriptsig[pos+4 : pos+8], "little")
    return idx, scriptsig[pos+8 : pos+8+text_len]


def main():
    corpus = CORPUS_PATH.read_bytes()
    total_chunks = (len(corpus) + CHUNK - 1) // CHUNK
    print(f"corpus: {len(corpus):,} bytes  → {total_chunks:,} chunks of {CHUNK}B each")

    tip = int(cli("getblockcount"))
    print(f"tip: {tip:,}")
    print(f"auditing heights {CODEX_DESCENT_START:,} .. {tip:,} ({tip - CODEX_DESCENT_START + 1:,} blocks)\n")

    fails, missing_egg, dreaming = [], [], []
    descent_ok = canon_ok = 0
    counter = 0
    t0 = time.time()

    for h in range(CODEX_DESCENT_START, tip + 1):
        blk_hash = cli("getblockhash", str(h))
        raw = bytes.fromhex(cli("getblock", blk_hash, "false"))
        scriptsig = coinbase_scriptsig(raw)
        off1 = find_off1(scriptsig)

        if off1 is None:
            missing_egg.append(h)
        else:
            idx, text = off1
            if idx == CODEX_MILESTONE:
                exp = DESCENT.get(h)
                if exp is None:
                    fails.append((h, "MILESTONE outside Descent range", text, b""))
                elif text != exp.encode("latin-1"):
                    fails.append((h, "Descent mismatch", text, exp.encode("latin-1")))
                else:
                    descent_ok += 1
            elif idx == CODEX_DREAMING:
                dreaming.append((h, text))
            else:
                exp_idx = h - CODEX_ANCHOR
                if idx != exp_idx:
                    fails.append((h, f"chunk-idx mismatch: got {idx}, expected {exp_idx}", text, b""))
                else:
                    start = idx * CHUNK
                    end = min(start + CHUNK, len(corpus))
                    exp = corpus[start:end]
                    if text != exp:
                        fails.append((h, "canon text mismatch", text, exp))
                    else:
                        canon_ok += 1

        counter += 1
        if counter % 200 == 0:
            rate = counter / (time.time() - t0)
            print(f"  ... h={h:,}  ({counter:,} done, {rate:.1f}/s)")

    print()
    print("=" * 70)
    print(f"Descent verses OK:  {descent_ok}/10")
    print(f"Canon chunks OK:    {canon_ok:,}")
    print(f"Dreaming blocks:    {len(dreaming):,}")
    print(f"Missing OFF1 egg:   {len(missing_egg):,}  (non-Codex miners)")
    print(f"Failures:           {len(fails):,}")

    if missing_egg:
        print("\nblocks without OFF1:")
        if len(missing_egg) <= 25:
            for h in missing_egg: print(f"  h={h:,}")
        else:
            print(f"  first 10: {missing_egg[:10]}")
            print(f"  last 5:   {missing_egg[-5:]}")

    if fails:
        print("\nFAILURES:")
        for h, why, got, exp in fails[:20]:
            print(f"  h={h:,}  {why}")
            print(f"    got:      {got!r}")
            print(f"    expected: {exp!r}")
        if len(fails) > 20:
            print(f"  ... ({len(fails) - 20} more)")
    elif not missing_egg:
        print("\nCodex is byte-clean against the canonical corpus.")


if __name__ == "__main__":
    main()
