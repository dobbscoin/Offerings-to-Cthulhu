#!/usr/bin/env python3
"""OFF Chain Codex e-reader. Renders the 23-work Lovecraft corpus as a Library of
paginated, paragraph-reflowed book pages, with a live 'inscribed on-chain' overlay
showing how far the immutable coinbase transcription (begins block 1,000,001) has
reached. Books are readable now; the chain fills the inscription bar over time."""
import json, os, html, re, subprocess, time, hashlib

# SGF-TTS-CACHE
TTS_MANIFEST = []
TTS_MANIFEST_OUT = "/var/www/23skidoo.info/codex/tts-pages.json"

# SGF-HEADER-CONSTANTS
HEADER_HTML = """<header class="site-header">
<h1><a href="/">Cthulhu<img src="/static/img/off_240x240.png" alt="Offerings to Cthulhu — home"></a>Offerings</h1>
<p class="site-tagline">He dreams no longer; nor does he wake.<br>The Great Old One is held at the door.</p>
</header>"""

FOOTER_HTML = """<footer class="site-foot">
<p>&copy; 2026 <a href="https://subgenius.finance/">SubGenius.Finance</a></p>
<p><a href="https://subgenius.finance/">SubGenius.Finance</a>: Where Sub-Culture Becomes Capital</p>
<p class="off-chant">ph&rsquo;nglui mglw&rsquo;nafh Cthulhu R&rsquo;lyeh wgah&rsquo;nagl fhtagn &middot; Praise Cthulhu</p>
<ul class="off-socials" aria-label="Offerings to Cthulhu community links">
<li><a href="https://23skidoo.info/" target="_blank" rel="noopener" aria-label="Offerings to Cthulhu"><img src="https://23skidoo.info/static/img/off_240x240.png" alt="Offerings to Cthulhu"></a></li>
<li><a href="https://pool.23skidoo.info/" target="_blank" rel="noopener" aria-label="OFF Mining Pool"><img src="https://api.iconify.design/mdi/pickaxe.svg?color=%235fffd0" alt="Pool"></a></li>
<li><a href="https://23skidoo.info/codex/" target="_blank" rel="noopener" aria-label="The Codex"><img src="https://cdn.simpleicons.org/bookstack/5fffd0" alt="Codex"></a></li>
<li><a href="https://github.com/SubGeniusFinance/Offerings-to-Cthulhu" target="_blank" rel="noopener" aria-label="GitHub"><img src="https://cdn.simpleicons.org/github/5fffd0" alt="GitHub"></a></li>
</ul>
</footer>"""

# SGF-NAV-CONSTANTS
# The gold button bar at the top of the site index, lifted at build time out of the
# live page itself — SITE_INDEX is the single source of truth for these buttons, so a
# release-version bump there reaches the codex on the next cron tick with no second edit.
# (The docroot copy, not contrib/site/index.html: the site index is maintained live and
# the repo copy has drifted behind it.)
# Namespaced .sitenav / .sitenav-row because the codex CSS already owns .nav and
# .nav button for the reader's prev/next pager.
SITE_INDEX = "/var/www/23skidoo.info/index.html"

# Only reached if the site index is unreadable or its markup stops matching. Deliberately
# version-free so a stale fallback can never advertise a download that has been superseded.
NAV_FALLBACK = """<nav class="sitenav">
  <span class="sitenav-row">
    <a class="btn" href="https://explorer.23skidoo.info/" target="_blank" rel="noopener">BLOCK EXPLORER</a>
    <a class="btn" href="https://pool.23skidoo.info/" target="_blank" rel="noopener">MINING POOL</a>
    <a class="btn" href="https://github.com/SubGeniusFinance/Offerings-to-Cthulhu" target="_blank" rel="noopener">GITHUB SOURCE</a>
    <a class="btn" href="https://github.com/SubGeniusFinance/Offerings-to-Cthulhu/releases/latest" target="_blank" rel="noopener">WALLETS</a>
  </span>
  <a class="btn btn-rlyehian" href="/rlyehian/">R&rsquo;LYEHIAN TRANSLATOR</a>
</nav>"""

def site_nav_html():
    """Return the site index's <nav> block, re-namespaced for the codex."""
    try:
        m = re.search(r'<nav class="nav">.*?</nav>',
                      open(SITE_INDEX, encoding="utf-8").read(), re.S)
        if not m:
            return NAV_FALLBACK
        return (m.group(0)
                .replace('<nav class="nav">', '<nav class="sitenav">')
                .replace('class="nav-row"', 'class="sitenav-row"'))
    except Exception:
        return NAV_FALLBACK

NAV_HTML = site_nav_html()

# Laid out so it survives either shape of the source markup: buttons wrapped in a
# .sitenav-row span, or dropped straight into the <nav> as they were before 2026-08.
NAV_CSS = """
.sitenav{display:flex;flex-wrap:wrap;justify-content:center;align-items:center;gap:5px;
  max-width:760px;margin:.6em auto 1.6em;padding:0 .4em}
.sitenav-row{display:flex;flex-basis:100%;justify-content:center;gap:5px;flex-wrap:wrap}
.sitenav .btn-rlyehian{flex-basis:94%;text-align:center}
.sitenav .btn{
  display:inline-block;padding:5px 10px;
  background:linear-gradient(to bottom,#5a3e00 0%,#a07710 16%,#ffd700 50%,#a07710 84%,#5a3e00 100%);
  color:#1a1100;
  font-family:"Trebuchet MS","Segoe UI",Tahoma,sans-serif;
  font-weight:900;font-size:.72rem;letter-spacing:.03em;
  text-transform:uppercase;text-decoration:none;
  border:1px solid rgba(120,80,0,.85);border-radius:4px;
  text-shadow:0 0 1px rgba(0,0,0,.55);
  box-shadow:inset 0 1px 0 rgba(255,238,160,.55),inset 0 -1px 0 rgba(60,40,0,.55),
    0 2px 4px rgba(0,0,0,.55),0 0 14px rgba(255,215,0,.18);
  transition:filter .15s,box-shadow .15s,transform .1s}
.sitenav .btn:visited{color:#1a1100}
.sitenav .btn:hover{
  filter:brightness(1.08) saturate(1.1);text-decoration:none;
  box-shadow:inset 0 1px 0 rgba(255,245,180,.7),inset 0 -1px 0 rgba(60,40,0,.5),
    0 2px 4px rgba(0,0,0,.45),0 0 22px rgba(255,215,0,.4)}
.sitenav .btn:active{
  transform:translateY(1px);
  background:linear-gradient(to bottom,#3a2700 0%,#7a5a08 18%,#c89e10 50%,#7a5a08 82%,#3a2700 100%)}
@media (max-width:640px){
  .sitenav{gap:4px;margin:.4em auto 1.2em}
  .sitenav-row{flex-wrap:wrap}
  .sitenav .btn{font-size:.7rem;padding:6px 9px;letter-spacing:.03em}}
"""
HEADER_CSS = """
.site-header{text-align:center;margin:0 0 1.4em;padding-top:6px}
.site-header h1{font-size:2rem;letter-spacing:.04em;margin:0;font-weight:normal;text-shadow:0 0 22px rgba(95,208,160,.4);color:var(--ink,#cfeee0)}
.site-header h1 a{color:inherit;text-decoration:none;border:none !important;display:inline-block}
.site-header h1 a:hover{text-decoration:none;filter:brightness(1.1)}
.site-header h1 img{vertical-align:middle;max-width:130px;height:auto;margin:0 .3em;filter:drop-shadow(0 0 18px rgba(95,208,160,.25))}
.site-tagline{text-align:center;font-style:italic;color:#FFD700;border-top:1px solid #16302a;border-bottom:1px solid #16302a;padding:.85em 0;margin:1.2em auto 2em;max-width:720px;letter-spacing:.04em;font-size:.95rem;line-height:1.55}
@media (max-width:640px){.site-header h1{font-size:1.3rem}.site-header h1 img{max-width:64px;margin:0 .2em}.site-tagline{font-size:.78rem;padding:.55em 0;margin:.9em auto 1.2em}.wrap{padding:18px 14px 60px}}
.site-foot{text-align:center;color:var(--accent,#5fd0a0);font-size:.82rem;margin:3.2em auto 0;padding-top:1.8em;border-top:1px solid #16302a;line-height:1.85;max-width:720px}
.site-foot p{margin:.35em 0}
.site-foot a{color:var(--accent,#5fd0a0);text-decoration:none;border:none}
.site-foot a:hover{text-decoration:none;filter:brightness(1.1)}
.site-foot .off-socials,ul.off-socials{position:fixed;bottom:14px;right:14px;z-index:25;display:flex;gap:10px;list-style:none;padding:0;margin:0;align-items:center;flex-wrap:nowrap;justify-content:flex-end}
.site-foot .off-socials li,ul.off-socials li{margin:0;padding:0}
.site-foot .off-socials a,ul.off-socials a{display:inline-flex;width:32px;height:32px;align-items:center;justify-content:center;border-radius:50%;background:rgba(20,20,20,.85);backdrop-filter:blur(2px);-webkit-backdrop-filter:blur(2px);border:0 !important;text-decoration:none;transition:transform .15s, background .15s}
.site-foot .off-socials a:hover,ul.off-socials a:hover{transform:translateY(-2px);background:rgba(40,40,40,.95)}
.site-foot .off-socials img,ul.off-socials img{width:18px;height:18px;display:block;border-radius:50%;opacity:1}
@media (max-width:640px){.site-foot{font-size:.74rem;margin-top:2em}}

.title-row{display:flex;justify-content:center;align-items:center;gap:1.1em;flex-wrap:wrap;margin:.2em 0 .4em}
.title-row h1{margin:0}
@media (max-width:640px){.title-row{gap:.5em}}
.tts-row{text-align:center;margin-top:14px}
.tts-btn{background:#0c1714;color:var(--accent);border:1px solid #16302a;border-radius:6px;padding:7px 16px;cursor:pointer;font-family:"Iowan Old Style",Palatino,Georgia,serif;font-size:.95rem;letter-spacing:.04em}
.tts-btn:hover{background:#0f1d1a}
.tts-btn.on{background:var(--accent);color:#05080a;border-color:var(--accent)}
@media (max-width:640px){.tts-btn{padding:8px 16px;font-size:.92rem}}
"""


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))   # contrib/codex/ in canonical repo
STATE_DIR  = "/home/btcbob/codex"                         # runtime caches + logs + secrets (gitignored by location)
CLI      = "/home/btcbob/claude/offerings-master/src/Offerings-cli"
CORPUS   = os.path.join(SCRIPT_DIR, "codex_corpus.txt")
MANIFEST = os.path.join(SCRIPT_DIR, "codex_manifest.json")
CACHE    = os.path.join(STATE_DIR, "ereader_cache.json")
OUTDIR   = "/var/www/23skidoo.info/codex"
ANCHOR = 1000001
CHUNK  = 48
PAGE_CHARS = 2600
MAGIC  = b"OFF1"

# Book 0 is the Proem (The Conclave's Invocation) — Conclave-authored, always readable.
# Books 1-23 are the Lovecraft canon — SEALED until at least one fragment is inscribed.
PROEM_INDEX = 0
SEAL_PLACEHOLDER_HTML = (
    '<div class="seal-mark">&mdash;&nbsp; SEALED &nbsp;&mdash;</div>'
    '<p class="seal-body">Awaiting inscription. This work will be transcribed '
    "fragment by fragment into the Offerings chain beginning at block 1,000,001, "
    "one ~48-byte chunk per block. Return when the Old One speaks.</p>"
)

def cli(*a):
    try: return subprocess.check_output([CLI,*a],text=True,timeout=30).strip()
    except Exception: return ""

# ---- on-chain inscription progress (contiguous chunks from idx 0) ------------
def inscribed_bytes():
    cache = {"scanned": ANCHOR-1, "have": []}
    if os.path.exists(CACHE):
        try: cache = json.load(open(CACHE))
        except Exception: pass
    tips = cli("getblockcount")
    if not tips.isdigit(): 
        have=set(cache.get("have",[]))
    else:
        tip=int(tips); have=set(cache.get("have",[]))
        for h in range(max(ANCHOR,cache["scanned"]+1), tip+1):
            try:
                bh=cli("getblockhash",str(h))
                blk=json.loads(cli("getblock",bh)); txid=blk["tx"][0]
                cb=json.loads(cli("getrawtransaction",txid,"1"))["vin"][0]["coinbase"]
                b=bytes.fromhex(cb); p=b.find(MAGIC)
                if p>=1:
                    L=b[p-1]
                    if 8<=L<=len(b)-p:
                        idx=int.from_bytes(b[p+4:p+8],"little")
                        if idx!=0xFFFFFFFF: have.add(idx)
            except Exception: pass
        cache={"scanned":tip,"have":sorted(have)}
        json.dump(cache,open(CACHE,"w"))
    # contiguous run from 0
    n=0
    while n in have: n+=1
    return n*CHUNK

# ---- load corpus + manifest --------------------------------------------------
data = open(CORPUS,"rb").read()
man  = json.load(open(MANIFEST))
books = man["books"]
ins = inscribed_bytes()

def reflow_paragraphs(text):
    # GB wraps lines inside paragraphs with single \n; blank line = paragraph break
    paras=[]
    for blk in re.split(r'\n\s*\n', text):
        blk=blk.strip()
        if not blk: continue
        paras.append(re.sub(r'\s*\n\s*',' ', blk))
    return paras

CSS = """
:root{--ink:#cfeee0;--dim:#5fffd0;--bg:#05080a;--panel:#0a1014;--accent:#5fd0a0;}
html{background-color:#05080a}
*{box-sizing:border-box} body{margin:0;background:transparent;position:relative;
color:var(--ink);font-family:"Iowan Old Style","Palatino Linotype",Georgia,serif;line-height:1.75;}
/* mobile-safe full-bleed background via fixed pseudo-element */
body::before{content:"";position:fixed;top:0;left:0;width:100vw;height:100vh;z-index:-1;background:linear-gradient(rgba(5,8,10,0.70),rgba(5,8,10,0.88)),url("/static/img/cthulhu-bg.jpg") center / cover no-repeat}
a{color:var(--accent);text-decoration:none} a:hover{text-decoration:underline}
.wrap{max-width:760px;margin:0 auto;padding:40px 22px 90px}
h1{text-align:center;font-size:2rem;letter-spacing:.03em;text-shadow:0 0 18px rgba(95,208,160,.35);margin:.2em 0}
.sub{text-align:center;color:var(--dim);font-style:italic;margin-bottom:2.2em}
.book{display:flex;align-items:center;gap:14px;background:var(--panel);border:1px solid #16302a;
border-radius:10px;padding:13px 18px;margin:9px 0}
.book .n{font-family:monospace;color:var(--dim);width:2.2em;text-align:right}
.book .t{flex:1;font-size:1.12rem}
.book .bar{width:120px;height:8px;background:#0c1714;border:1px solid #16302a;border-radius:5px;overflow:hidden}
.book .bar>i{display:block;height:100%;background:linear-gradient(90deg,#1f6f55,var(--accent))}
.book .pct{font-family:monospace;font-size:.74rem;color:var(--dim);width:3.4em;text-align:right}
.reader{background:var(--panel);border:1px solid #16302a;border-radius:10px;padding:34px 38px;min-height:60vh}
.reader p{margin:0 0 1em;text-indent:1.6em;text-align:justify}
.reader p:first-child{text-indent:0}
.page{display:none} .page.on{display:block}
.nav{display:flex;justify-content:space-between;align-items:center;margin-top:20px;font-family:monospace;color:var(--dim)}
.nav button{background:#0c1714;color:var(--accent);border:1px solid #16302a;border-radius:6px;padding:8px 16px;cursor:pointer;font-size:1rem}
.nav button:disabled{opacity:.3;cursor:default}
.crumb{font-family:monospace;font-size:.8rem;color:var(--dim);margin-bottom:1.4em}
.inscr{font-family:monospace;font-size:.74rem;color:var(--dim);text-align:center;margin-top:10px}
.foot{text-align:center;color:var(--dim);font-size:.76rem;margin-top:2.4em;font-style:italic}
.sealed{text-align:center;padding:3em 1.5em;color:var(--dim);font-style:italic}
.seal-mark{font-family:monospace;font-style:normal;letter-spacing:.5em;font-size:.85rem;color:var(--accent);margin-bottom:2.2em;text-shadow:0 0 14px rgba(95,208,160,.25)}
.seal-body{text-indent:0;text-align:center;max-width:36em;margin:0 auto;line-height:1.85}
"""

os.makedirs(OUTDIR, exist_ok=True)

# ---- per-book reader pages ---------------------------------------------------
for i,b in enumerate(books):
    bs,bl=b["body_start"],b["body_len"]
    overlap=max(0,min(ins,bs+bl)-bs); pct=int(100*overlap/bl) if bl else 0
    title=html.escape(b["title"])

    # SEAL gate: books 1-23 with zero inscribed bytes render a placeholder only,
    # no body text, no TTS, no nav. Book 0 (Proem) is always open — it's the
    # Conclave's invocation, readable regardless of chain state.
    sealed = (i != PROEM_INDEX) and (overlap == 0)

    if sealed:
        pages_html = f'<div class="page on sealed" id="pg0">{SEAL_PLACEHOLDER_HTML}</div>'
        tts_btn_html = ""
        nav_html = ""
        script_html = ""
    else:
        body = data[bs:bs+bl].decode("latin-1","replace")
        paras = reflow_paragraphs(body)
        # paginate into ~PAGE_CHARS pages
        pages=[]; cur=[]; clen=0
        for p in paras:
            if clen+len(p) > PAGE_CHARS and cur:
                pages.append(cur); cur=[]; clen=0
            cur.append(p); clen+=len(p)
        if cur: pages.append(cur)
        if not pages: pages=[["(not yet transcribed)"]]
        pages_html=""
        for pi,pg in enumerate(pages):
            ps="".join(f"<p>{html.escape(x)}</p>" for x in pg)
            page_text = "\n\n".join(pg)
            page_key  = hashlib.sha256(page_text.encode("utf-8")).hexdigest()[:16]
            TTS_MANIFEST.append({"key": page_key, "text": page_text, "book": i, "page": pi, "title": b["title"]})
            pages_html+=f'<div class="page{" on" if pi==0 else ""}" id="pg{pi}" data-tts-key="{page_key}">{ps}</div>'
        tts_btn_html = '<button id="tts" class="tts-btn" type="button">&#9654; Read aloud</button>'
        nav_html = (
            f'<div class="nav"><button id="prev">&larr; prev</button>'
            f'<span id="ctr">page 1 / {len(pages)}</span>'
            f'<button id="next">next &rarr;</button></div>'
        )
        n_pages = len(pages)
        script_html = f"""<script>
/* SGF-TTS-INJECTED SGF-TTS-TITLE */
var pages=[...document.querySelectorAll('.page')],cur=0;
var ttsBtn=document.getElementById('tts');
var speaking=false;
function pickVoice(){{
  var vs=window.speechSynthesis?speechSynthesis.getVoices():[];
  var want=['Daniel','Rishi','Eddy','Albert','Fred','Ralph','Bahh','Reed','Aaron','Diego','Arthur','Oliver'];
  for(var i=0;i<want.length;i++){{var v=vs.find(x=>x.name.indexOf(want[i])>=0&&x.lang.indexOf('en')===0);if(v)return v;}}
  return vs.find(v=>v.lang.indexOf('en')===0)||vs[0];
}}
function ttsLabel(on){{ttsBtn.innerHTML=on?'&#9209; Stop reading':'&#9654; Read aloud';ttsBtn.classList.toggle('on',on);}}
function ttsStop(){{if(window.speechSynthesis){{speechSynthesis.cancel();}}if(window._ttsAudio){{try{{window._ttsAudio.pause();window._ttsAudio.currentTime=0;}}catch(e){{}}}}speaking=false;ttsLabel(false);}}
function speakBrowser(txt){{
  if(!window.speechSynthesis){{ttsStop();return;}}
  speechSynthesis.cancel();
  var u=new SpeechSynthesisUtterance(txt);
  var v=pickVoice();if(v)u.voice=v;
  u.rate=0.82; u.pitch=0.55; u.volume=1.0;
  u.onend=ttsStop; u.onerror=ttsStop;
  speechSynthesis.speak(u);
}}
function ttsToggle(){{
  if(speaking){{ttsStop();return;}}
  var key=pages[cur].getAttribute('data-tts-key');
  var txt=pages[cur].innerText.trim(); if(!txt) return;
  speaking=true; ttsLabel(true);
  if(key){{
    fetch('/static/tts/'+key+'.mp3',{{method:'HEAD'}}).then(function(r){{
      if(r.ok){{
        if(!window._ttsAudio) window._ttsAudio = new Audio();
        window._ttsAudio.src='/static/tts/'+key+'.mp3';
        window._ttsAudio.onended=ttsStop;
        window._ttsAudio.onerror=function(){{speakBrowser(txt);}};
        var pr=window._ttsAudio.play();
        if(pr&&pr.catch) pr.catch(function(){{speakBrowser(txt);}});
      }} else speakBrowser(txt);
    }}).catch(function(){{speakBrowser(txt);}});
  }} else speakBrowser(txt);
}}
ttsBtn.onclick=ttsToggle;
if(window.speechSynthesis&&speechSynthesis.getVoices().length===0){{
  speechSynthesis.addEventListener('voiceschanged',function(){{}},{{once:true}});
}}
function show(n){{ttsStop();
pages[cur].classList.remove('on');cur=Math.max(0,Math.min(pages.length-1,n));
pages[cur].classList.add('on');document.getElementById('ctr').textContent='page '+(cur+1)+' / '+pages.length;
document.getElementById('prev').disabled=cur==0;document.getElementById('next').disabled=cur==pages.length-1;
window.scrollTo(0,0);}}
document.getElementById('prev').onclick=()=>show(cur-1);
document.getElementById('next').onclick=()=>show(cur+1);
document.addEventListener('keydown',e=>{{if(e.key=='ArrowLeft')show(cur-1);if(e.key=='ArrowRight')show(cur+1);}});
window.addEventListener('beforeunload',ttsStop);
show(0);
</script>"""

    page=f"""<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title} — The Chain Codex</title><style>{CSS}{HEADER_CSS}</style></head><body><div class="wrap">{HEADER_HTML}
<div class="crumb"><a href="index.html">&larr; The Library</a> &middot; book {i+1} of {len(books)}</div>
<div class="title-row"><h1>{title}</h1>{tts_btn_html}</div>
<div class="reader" id="reader">{pages_html}</div>
{nav_html}
<div class="inscr">inscribed on-chain: {pct}% &middot; {overlap:,} / {bl:,} bytes</div>
<div class="foot"><em>Ph&#39;nglui mglw&#39;nafh Cthulhu R&#39;lyeh wgah&#39;nagl fhtagn.</em></div>
{FOOTER_HTML}
</div>{script_html}</body></html>"""
    open(f"{OUTDIR}/book_{i}.html","w").write(page)

# ---- Library index -----------------------------------------------------------
rows=""
for i,b in enumerate(books):
    bs,bl=b["body_start"],b["body_len"]
    overlap=max(0,min(ins,bs+bl)-bs); pct=int(100*overlap/bl) if bl else 0
    rows+=f'''<a class="book" href="book_{i}.html"><span class="n">{i+1}</span>
<span class="t">{html.escape(b["title"])}</span>
<span class="bar"><i style="width:{pct}%"></i></span><span class="pct">{pct}%</span></a>'''
tip=cli("getblockcount") or "?"
total_ins = sum(min(ins,b["body_start"]+b["body_len"])-b["body_start"] for b in books if ins>b["body_start"])
total_ins=max(0,total_ins)
overall=int(100*ins/len(data)) if len(data) else 0
ts=time.strftime("%Y-%m-%d %H:%M UTC",time.gmtime())
idx=f"""<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="refresh" content="120">
<title>The Chain Codex — Library</title><style>{CSS}{HEADER_CSS}{NAV_CSS}</style></head><body><div class="wrap">{HEADER_HTML}
{NAV_HTML}
<h1>The Chain Codex</h1>
<div class="sub">The public-domain Lovecraft canon, transcribed block by block into the Offerings to Cthulhu ledger.<br>
Read freely below; each work's bar shows how much has been permanently inscribed on-chain.</div>
{rows}
<div class="foot">Chain height {tip} &middot; {overall}% of the canon inscribed on-chain &middot; {"the canon is complete &mdash; the chain now speaks in hash-seeded R&rsquo;lyehian, the Dreaming" if overall >= 100 else "transcription begins at the Awakening (block 1,000,000)"} &middot; updated {ts}<br>
<em>That is not dead which can eternal lie, and with strange aeons even death may die.</em></div>
{FOOTER_HTML}
</div></body></html>"""
open(f"{OUTDIR}/index.html","w").write(idx)
print(f"e-reader built: {len(books)} books, inscribed={ins} bytes, overall {overall}%")

# SGF-TTS-CACHE write manifest
try:
    import json as _json, os as _os
    _os.makedirs(_os.path.dirname(TTS_MANIFEST_OUT), exist_ok=True)
    with open(TTS_MANIFEST_OUT, "w") as _fh:
        _json.dump(TTS_MANIFEST, _fh)
except Exception as _e:
    print("tts-manifest write failed:", _e)
