#!/usr/bin/env python3
"""Publish the chain's recent Dreaming verses as JSON for the 23skidoo.info
front page ("bubbling up from the deep" ambience). Post-canon blocks inscribe
hash-seeded R'lyehian with chunk idx 0xFFFFFFFE (CODEX_DREAMING); build_codex's
cache keys chunks by idx so consecutive dreams overwrite each other there —
this scanner keeps them per-height instead.

Output: /var/www/23skidoo.info/static/dreams.json
  {"tip": <height>, "dreams": [{"h": <height>, "t": "<verse>"}, ...]}  newest first
"""
import subprocess, json, os

CLI      = "/home/btcbob/claude/offerings-master/src/Offerings-cli"
OUT      = "/var/www/23skidoo.info/static/dreams.json"
CACHE    = "/home/btcbob/codex/dreams_cache.json"
MAGIC    = b"OFF1"
DREAMING = 0xFFFFFFFE
KEEP     = 40      # verses to publish
SCAN_MAX = 400     # deepest first-run backscan

def cli(*a):  return subprocess.check_output([CLI, *a], text=True, timeout=30).strip()
def cj(*a):   return json.loads(cli(*a))

def decode(hexstr):
    b = bytes.fromhex(hexstr); p = b.find(MAGIC)
    if p < 1: return None
    L = b[p-1]
    if L < 8 or p + L > len(b): return None
    return int.from_bytes(b[p+4:p+8], "little"), b[p+8:p+L].decode("latin-1", "replace")

cache = {"scanned": 0, "dreams": []}
if os.path.exists(CACHE):
    try: cache = json.load(open(CACHE))
    except Exception: pass

tip = int(cli("getblockcount"))
start = max(cache["scanned"] + 1, tip - SCAN_MAX + 1)
for h in range(start, tip + 1):
    try:
        blk = cj("getblock", cli("getblockhash", str(h)))
        cb  = cj("getrawtransaction", blk["tx"][0], "1")["vin"][0]["coinbase"]
        d = decode(cb)
        if d and d[0] == DREAMING and d[1].strip():
            cache["dreams"].append({"h": h, "t": d[1].strip()})
    except Exception:
        pass
cache["scanned"] = tip
cache["dreams"] = cache["dreams"][-200:]
json.dump(cache, open(CACHE + ".tmp", "w")); os.replace(CACHE + ".tmp", CACHE)

out = {"tip": tip, "dreams": list(reversed(cache["dreams"][-KEEP:]))}
json.dump(out, open(OUT + ".tmp", "w")); os.replace(OUT + ".tmp", OUT)
print(f"tip {tip}, published {len(out['dreams'])} dreams")
