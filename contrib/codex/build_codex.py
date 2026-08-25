#!/usr/bin/env python3
"""OFF Chain Codex builder. Reassembles the Lovecraft transcription embedded in
coinbase scriptSigs (wire: "OFF1" | uint32-LE idx | text). idx 0xFFFFFFFF marks the
milestone "Descent" verses (blocks 999991..1000000), shown separately. Incremental."""
import subprocess, json, html, os, time

STATE_DIR = "/home/btcbob/codex"   # runtime caches (gitignored by location)
CLI    = "/usr/local/bin/Offerings-cli"   # Phase B 2026-08-16: installed artifact, not the git tree
DDIR = "-datadir=/var/lib/offeringsd"   # Phase B: btcbob no longer has a default datadir
ANCHOR = 1000001
FORK   = 1000000
MILE   = 0xFFFFFFFF
OUT    = "/var/www/23skidoo.info/codex/index.html"
CACHE  = os.path.join(STATE_DIR, "codex_cache.json")
MAGIC  = b"OFF1"

def cli(*a):  return subprocess.check_output([CLI, DDIR, *a], text=True, timeout=30).strip()
def cj(*a):   return json.loads(cli(*a))

def decode(hexstr):
    b = bytes.fromhex(hexstr); p = b.find(MAGIC)
    if p < 1: return None
    L = b[p-1]
    if L < 8 or p + L > len(b): return None
    return int.from_bytes(b[p+4:p+8], "little"), b[p+8:p+L].decode("latin-1", "replace")

cache = {"scanned": ANCHOR-1, "chunks": {}, "descent": {}}
if os.path.exists(CACHE):
    try:
        cache = json.load(open(CACHE)); cache.setdefault("descent", {})
    except Exception: pass

tip = int(cli("getblockcount"))
for h in range(max(ANCHOR, cache["scanned"]+1), tip+1):
    try:
        blk = cj("getblock", cli("getblockhash", str(h)))
        cb  = cj("getrawtransaction", blk["tx"][0], "1")["vin"][0]["coinbase"]
        d = decode(cb)
        if d:
            idx, text = d
            if idx == MILE: cache["descent"][str(h)] = text
            else:           cache["chunks"][str(idx)] = {"text": text, "height": h}
    except Exception: pass
cache["scanned"] = tip
json.dump(cache, open(CACHE, "w"))

chunks  = cache["chunks"]; descent = cache["descent"]
maxidx  = max((int(k) for k in chunks), default=-1)
manuscript = "".join((chunks.get(str(i), {}).get("text", " … ")) for i in range(maxidx+1))
revealed   = len(chunks)
awakened   = tip >= FORK

# descent / awakening section
desc_rows = ""
if descent:
    for h in sorted(int(k) for k in descent):
        cls = "awaken" if h == FORK else ""
        desc_rows += f"<tr class='{cls}'><td>{h:,}</td><td>{html.escape(descent[str(h)])}</td></tr>"
desc_html = f"""<h2>The Descent &mdash; the Awakening Verses</h2>
  <table><tr><th>block</th><th>verse</th></tr>{desc_rows}</table>""" if descent else ""

banner = ('<div class="awaken-banner">THE THRESHOLD HOLDS &mdash; CTHULHU FHTAGN</div>'
          if awakened else "")

rows = []
for i in range(maxidx+1):
    c = chunks.get(str(i))
    rows.append(f"<tr><td>{i}</td><td>{c['height']}</td><td>{html.escape(c['text'])}</td></tr>"
                if c else f"<tr class='missing'><td>{i}</td><td>&mdash;</td><td><em>not yet transcribed</em></td></tr>")
ledger = "\n".join(rows)
ts = time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime())

page = f"""<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="60">
<title>The Chain Codex &mdash; Offerings to Cthulhu</title>
<style>
  :root {{ --ink:#9fe8c9; --dim:#5fffd0; --panel:#0a1014; --accent:#5fd0a0; --awk:#d96bb0; }}
  html {{ background-color:#05080a; }}
  *{{box-sizing:border-box}} body {{ margin:0;
    background:transparent; position:relative;
    color:var(--ink); font-family:"Iowan Old Style","Palatino Linotype",Georgia,serif; line-height:1.7; }}
  /* mobile-safe full-bleed background via fixed pseudo-element */
  body::before {{ content:""; position:fixed; top:0; left:0;
    width:100vw; height:100vh; z-index:-1;
    background:linear-gradient(rgba(5,8,10,0.70), rgba(5,8,10,0.88)), url("/static/img/cthulhu-bg.jpg") center / cover no-repeat; }}
  .wrap {{ max-width:820px; margin:0 auto; padding:48px 24px 96px; }}
  h1 {{ font-size:2.2rem; text-align:center; margin:0 0 .2em; text-shadow:0 0 18px rgba(95,208,160,.35); }}
  .sub {{ text-align:center; color:var(--dim); font-style:italic; margin-bottom:2em; }}
  .awaken-banner {{ text-align:center; color:var(--awk); font-family:monospace; letter-spacing:.1em;
    border:1px solid #4a1c3a; background:#160a12; border-radius:8px; padding:14px; margin-bottom:2em;
    text-shadow:0 0 18px rgba(217,107,176,.6); }}
  .stats {{ display:flex; gap:14px; justify-content:center; flex-wrap:wrap; margin-bottom:2.4em; }}
  .stat {{ background:var(--panel); border:1px solid #16302a; border-radius:10px; padding:12px 18px; text-align:center; min-width:120px; }}
  .stat b {{ display:block; font-size:1.5rem; color:var(--accent); font-family:monospace; }}
  .stat span {{ font-size:.72rem; text-transform:uppercase; letter-spacing:.12em; color:var(--dim); }}
  .manuscript {{ background:var(--panel); border:1px solid #16302a; border-left:3px solid var(--accent);
    border-radius:8px; padding:28px 30px; font-size:1.18rem; white-space:pre-wrap; box-shadow:0 0 60px rgba(0,0,0,.6) inset; }}
  .cursor {{ color:var(--accent); animation:blink 1.2s steps(2) infinite; }} @keyframes blink {{ 50%{{opacity:0}} }}
  h2 {{ margin-top:3em; font-size:1rem; text-transform:uppercase; letter-spacing:.18em; color:var(--dim);
    border-bottom:1px solid #16302a; padding-bottom:.5em; }}
  table {{ width:100%; border-collapse:collapse; font-size:.86rem; font-family:monospace; }}
  td,th {{ text-align:left; padding:6px 10px; border-bottom:1px solid #0f1d1a; vertical-align:top; }}
  th {{ color:var(--dim); text-transform:uppercase; font-size:.7rem; letter-spacing:.1em; }}
  tr.missing td {{ color:var(--dim); }}
  tr.awaken td {{ color:var(--awk); text-shadow:0 0 10px rgba(217,107,176,.5); }}
  .foot {{ text-align:center; color:var(--dim); font-size:.78rem; margin-top:3em; font-style:italic; }}
  a {{ color:var(--accent); }}
</style></head><body><div class="wrap">
  <h1>The Chain Codex</h1>
  <div class="sub">A transcription written, block by block, into the Offerings to Cthulhu ledger.</div>
  {banner}
  <div class="stats">
    <div class="stat"><b>{tip:,}</b><span>chain height</span></div>
    <div class="stat"><b>{revealed}</b><span>fragments</span></div>
    <div class="stat"><b>{len(descent)}</b><span>descent verses</span></div>
    <div class="stat"><b>{ANCHOR:,}</b><span>codex begins</span></div>
  </div>
  <div class="manuscript">{html.escape(manuscript)}<span class="cursor">&#9608;</span></div>
  {desc_html}
  <h2>The Ledger of Fragments</h2>
  <table><tr><th>#</th><th>block</th><th>inscription</th></tr>{ledger}</table>
  <div class="foot">Generated {ts}. Each fragment lives immutably in a block's coinbase. When the canon is
    exhausted the chain ceases to quote and begins to speak.<br>
    <em>Ph&#39;nglui mglw&#39;nafh Cthulhu R&#39;lyeh wgah&#39;nagl fhtagn.</em></div>
</div></body></html>"""
open(OUT+".tmp","w").write(page); os.replace(OUT+".tmp", OUT)
print(f"codex built: tip={tip} revealed={revealed} descent={len(descent)} awakened={awakened}")
