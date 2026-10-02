#!/usr/bin/env python3
"""Live status page for 23skidoo.info/awakening/.

Pre-fork: countdown to block 1,000,000 (the original behavior).
Post-fork: proclamation page — "THE THRESHOLD HOLDS" — with live tip,
days-since-Awakening, the Ritual Renewed (current rite or countdown to the
next finale, mirrored from RitualBonus() in src/main.cpp), Reclamation-close
countdown, plus the canonical "since the Awakening" / "what activated" /
"how to help" sections.

Run on a 1-minute cron."""
import subprocess, json, os, time

# SGF-HEADER-CONSTANTS
HEADER_HTML = """<header class="site-header">
<h1><a href="/">Cthulhu<img src="/static/img/off_240x240.png" alt="Offerings to Cthulhu — home"></a>Offerings</h1>
<p class="site-tagline">He dreams no longer; nor does he wake.<br>The Great Old One is held at the door.</p>
</header>"""

HEADER_CSS = """

.site-header{text-align:center;margin:0 0 1.4em;padding-top:6px}
.site-header h1{font-size:2rem;letter-spacing:.04em;margin:0;font-weight:normal;text-shadow:0 0 22px rgba(95,208,160,.4);color:var(--ink,#cfeee0)}
.site-header h1 a{color:inherit;text-decoration:none;border:none !important;display:inline-block}
.site-header h1 a:hover{text-decoration:none;filter:brightness(1.1)}
.site-header h1 img{vertical-align:middle;max-width:130px;height:auto;margin:0 .3em;filter:drop-shadow(0 0 18px rgba(95,208,160,.25))}
.site-tagline{text-align:center;font-style:italic;color:#FFD700;border-top:1px solid #16302a;border-bottom:1px solid #16302a;padding:.85em 0;margin:1.2em auto 2em;max-width:720px;letter-spacing:.04em;font-size:.95rem;line-height:1.55}
@media (max-width:640px){.site-header h1{font-size:1.3rem}.site-header h1 img{max-width:64px;margin:0 .2em}.site-tagline{font-size:.78rem;padding:.55em 0;margin:.9em auto 1.2em}.wrap{padding:18px 14px 60px}}
"""

FOOTER_HTML = """<footer class="site-foot">
<p>&copy; 2026 <a href="https://subgenius.finance/">SubGenius.Finance</a></p>
<p><a href="https://subgenius.finance/">SubGenius.Finance</a>: Where Sub-Culture Becomes Capital</p>
<p class="off-chant">ph&rsquo;nglui mglw&rsquo;nafh Cthulhu R&rsquo;lyeh wgah&rsquo;nagl fhtagn &middot; Praise Cthulhu</p>
<ul class="off-socials" aria-label="Offerings to Cthulhu community links">
<li><a href="https://23skidoo.info/" target="_blank" rel="noopener" aria-label="Offerings to Cthulhu"><img src="https://23skidoo.info/static/img/off_240x240.png" alt="Offerings to Cthulhu"></a></li>
<li><a href="https://pool.23skidoo.info/" target="_blank" rel="noopener" aria-label="OFF Mining Pool"><img src="https://api.iconify.design/mdi/pickaxe.svg?color=%235fffd0" alt="Pool"></a></li>
<li><a href="https://23skidoo.info/codex/" target="_blank" rel="noopener" aria-label="The Codex"><img src="https://cdn.simpleicons.org/bookstack/5fffd0" alt="Codex"></a></li>
<li><a href="https://git.subgenius.finance/SubGeniusFinance/Offerings-to-Cthulhu" target="_blank" rel="noopener" aria-label="Source code"><img src="https://cdn.simpleicons.org/forgejo/5fffd0" alt="Source code"></a></li>
</ul>
</footer>"""

FOOTER_CSS = """
.site-foot{text-align:center;color:var(--accent,#5fd0a0);font-size:.82rem;margin:3.2em auto 0;padding-top:1.8em;border-top:1px solid #16302a;line-height:1.85;max-width:720px}
.site-foot p{margin:.35em 0}
.site-foot a{color:var(--accent,#5fd0a0);text-decoration:none;border:none}
.site-foot a:hover{text-decoration:none;filter:brightness(1.1)}
@media (max-width:640px){.site-foot{font-size:.74rem;margin-top:2em}}

.site-foot .off-chant { font-style:italic; letter-spacing:.04em; }
.site-foot .off-socials, ul.off-socials {
  position:fixed; bottom:14px; right:14px; z-index:25;
  display:flex; gap:10px; list-style:none; padding:0; margin:0;
  align-items:center; flex-wrap:nowrap; justify-content:flex-end; }
.site-foot .off-socials li, ul.off-socials li { margin:0; padding:0; }
.site-foot .off-socials a, ul.off-socials a {
  display:inline-flex; width:32px; height:32px;
  align-items:center; justify-content:center;
  border-radius:50%; background:rgba(20,20,20,.85);
  backdrop-filter:blur(2px); -webkit-backdrop-filter:blur(2px);
  border:0 !important; text-decoration:none;
  transition:transform .15s, background .15s; }
.site-foot .off-socials a:hover, ul.off-socials a:hover {
  transform:translateY(-2px); background:rgba(40,40,40,.95); }
.site-foot .off-socials img, ul.off-socials img {
  width:18px; height:18px; display:block; border-radius:50%; opacity:1; }
"""

MQ_COUNTDOWN = "@media (max-width:640px){h1{font-size:1.5rem}.chant{font-size:.9rem;margin-bottom:1em}.countdown{padding:.85em 1em;gap:.3em;border-radius:36px;width:100%}.cd-blocks{font-size:1.35rem;letter-spacing:.06em;white-space:nowrap}.cd-tick{font-size:.82rem;letter-spacing:.16em;white-space:nowrap}.eta{font-size:.86rem;margin-bottom:.9em}.grid{gap:6px}.cell{min-width:0;padding:9px 11px;flex:1 1 90px}.cell b{font-size:1rem}.rules{padding:12px 16px;font-size:.9rem}.awaken-row{grid-template-columns:1fr !important;gap:10px !important}}"


STATE_DIR = "/home/btcbob/codex"   # runtime cache (gitignored by location)
CLI   = "/usr/local/bin/Offerings-cli"   # Phase B 2026-08-16: installed artifact, not the git tree
DDIR  = "-datadir=/var/lib/offeringsd"       # Phase B: btcbob no longer has a default datadir
FORK  = 1000000
OFFSIG_END = 1050666
CANON_END  = 1047248      # last canon chunk inscribed; the Dreaming begins at +1
ELDERSIGN  = 1055555      # BIP66 + BIP65 + 240-conf maturity + rolling checkpoints
FIRST_FINALE = 1141666    # Ritual Renewed — first 10,000 OFF finale (~autumnal equinox)
PERIOD       = 263000     # the rite recurs every 263,000 blocks (~6 months); MUST match src/main.cpp
DAY          = 1440       # blocks per chain-day at the 60s target
RECLAMATION_DAYS = 730    # 2-year window
RELEASE_TAG = "v2.1.3-Nodens"  # current release; bump in tandem with new tags


def ritual_bonus(h):
    """Mirror of RitualBonus() in src/main.cpp, in whole OFF. Display only."""
    if h < FIRST_FINALE - 28*DAY:
        return 0
    k = max(0, (h - FIRST_FINALE + PERIOD//2) // PERIOD)
    d = FIRST_FINALE + k*PERIOD - h
    if d < 0 or d > 28*DAY or d % DAY:
        return 0
    day = d // DAY
    return 10000 if day == 0 else 1000 if day <= 5 else 100 if day <= 12 else 10 if day <= 19 else 1 if day <= 26 else 0


def next_finale(h):
    """Next finale height at or above h (GetRitualStatus() in src/main.cpp)."""
    k = 0 if h <= FIRST_FINALE else (h - FIRST_FINALE + PERIOD - 1) // PERIOD
    return FIRST_FINALE + k*PERIOD
OUT   = "/var/www/23skidoo.info/awakening/index.html"
CACHE = os.path.join(STATE_DIR, "countdown_cache.json")

def cli(*a):
    return subprocess.check_output([CLI, DDIR, *a], text=True, timeout=30).strip()

now = int(time.time())
tip = int(cli("getblockcount"))

# rolling samples for rate estimation
samples = []
if os.path.exists(CACHE):
    try: samples = json.load(open(CACHE))
    except Exception: samples = []
samples.append([now, tip])
samples = [s for s in samples if s[0] >= now - 6*3600][-400:]   # keep last 6h
json.dump(samples, open(CACHE, "w"))

rate = None  # blocks per second
if len(samples) >= 2 and samples[-1][1] > samples[0][1]:
    dt = samples[-1][0] - samples[0][0]
    dh = samples[-1][1] - samples[0][1]
    if dt > 0 and dh > 0:
        rate = dh / dt
if not rate:
    rate = 1.0 / 60.0   # fall back to the 60s block target
bpm = rate * 60.0

# Common style block — reused by both pre-fork and post-fork pages
STYLE_BLOCK = f"""<style>
  :root {{ --ink:#9fe8c9; --dim:#5fffd0; --panel:#0a1014; --accent:#5fd0a0; --warn:#5fffd0; --gold:#FFD700; }}
  *{{box-sizing:border-box}}
  html {{ background-color:#05080a; }}
  body {{ margin:0; min-height:100vh; background:transparent; position:relative;
    color:var(--ink); font-family:"Iowan Old Style","Palatino Linotype",Georgia,serif; line-height:1.6; }}
  body::before {{ content:""; position:fixed; top:0; left:0;
    width:100vw; height:100vh; z-index:-1;
    background:linear-gradient(rgba(5,8,10,0.70), rgba(5,8,10,0.88)), url("/static/img/cthulhu-bg.jpg") center / cover no-repeat; }}
  .wrap {{ max-width:760px; margin:0 auto; padding:56px 24px 90px; text-align:center; }}
  .eyebrow {{ letter-spacing:.32em; text-transform:uppercase; font-size:.74rem; color:var(--dim); }}
  h1 {{ font-size:2.6rem; margin:.25em 0 .1em; text-shadow:0 0 22px rgba(95,208,160,.4); }}
  .chant {{ font-style:italic; color:var(--dim); margin-bottom:2.2em; }}
  .countdown {{ display:flex; flex-direction:column; align-items:center; justify-content:center;
    gap:.35em; width:min(620px,100%); margin:.6em auto 1.6em; padding:1.15em 2.2em;
    background:linear-gradient(180deg, rgba(10,16,20,.92), rgba(8,12,16,.92));
    border:1px solid rgba(95,208,160,.35); border-radius:999px;
    box-shadow: inset 0 0 26px rgba(95,208,160,.16), 0 0 42px rgba(95,208,160,.22);
    font-family:"JetBrains Mono","SF Mono",Menlo,Consolas,monospace; }}
  .cd-blocks {{ font-size:2.2rem; color:var(--accent); letter-spacing:.10em; line-height:1;
    text-shadow:0 0 18px rgba(95,208,160,.55); }}
  .cd-tick {{ font-size:1.05rem; color:var(--gold); letter-spacing:.22em; line-height:1;
    text-shadow:0 0 14px rgba(255,215,0,.55), 0 0 4px rgba(255,215,0,.4); opacity:.95; }}
  .eta {{ color:var(--warn); font-size:1.05rem; margin-bottom:1.8em; }}
  .eta > div {{ line-height:1.55; margin:.3em 0; }}
  .eta .nw {{ white-space:nowrap; }}
  .eta .sep {{ opacity:.45; margin:0 .25em; }}
  .eta .eta-prize {{ margin-top:.75em; letter-spacing:.04em; }}
  .bar {{ height:14px; background:#0c1714; border:1px solid #16302a; border-radius:8px; overflow:hidden; margin:1.4em 0 .4em; }}
  .bar > i {{ display:block; height:100%;
    background:linear-gradient(90deg,#1f6f55,var(--accent)); box-shadow:0 0 16px var(--accent); }}
  .barlabel {{ font-size:.8rem; color:var(--dim); margin-bottom:2.6em; }}
  .grid {{ display:flex; gap:14px; justify-content:center; flex-wrap:wrap; margin-bottom:2.4em; }}
  .cell {{ background:var(--panel); border:1px solid #16302a; border-radius:10px; padding:14px 20px; min-width:150px; }}
  .cell b {{ display:block; font-family:monospace; font-size:1.5rem; color:var(--accent); }}
  .cell span {{ font-size:.7rem; text-transform:uppercase; letter-spacing:.12em; color:var(--dim); }}
  .rules {{ text-align:left; background:var(--panel); border:1px solid #16302a; border-radius:10px;
    padding:18px 26px; margin:0 auto 2em; max-width:620px; }}
  .rules li {{ margin:.4em 0; }}
  a {{ color:var(--accent); }}
  .foot {{ color:var(--dim); font-size:.8rem; font-style:italic; margin-top:2em; }}
  .awaken-row {{ display:grid; grid-template-columns:repeat(3, 1fr); gap:14px; max-width:620px; margin:1.6em auto 2.2em; }}
  .awaken-link {{ display:flex; flex-direction:column; align-items:center; gap:.4em;
    padding:1.1em 1em; text-decoration:none; border:1px solid #16302a; border-radius:10px;
    background:var(--panel); transition:border-color .15s, box-shadow .15s, transform .15s; }}
  .awaken-link:hover {{ border-color:var(--accent); box-shadow:0 0 24px rgba(95,208,160,.18); transform:translateY(-1px); }}
  .awaken-link .eyebrow {{ font-size:.6rem; letter-spacing:.22em; }}
  .awaken-link strong {{ font-size:1.05rem; color:var(--accent); font-weight:normal; letter-spacing:.02em; text-align:center; }}
  .awaken-link .arrow {{ color:var(--accent); font-size:.78rem; opacity:.8; }}
  .release-row {{ display:flex; gap:10px; justify-content:center; flex-wrap:wrap; margin:1em auto 2em; max-width:620px; }}
  .release-btn {{ padding:.55em 1em; border:1px solid #16302a; border-radius:6px; background:rgba(10,16,20,.7);
    color:var(--accent); text-decoration:none; font-size:.78rem; letter-spacing:.06em; text-transform:uppercase;
    transition:border-color .15s, color .15s; }}
  .release-btn:hover {{ border-color:var(--accent); color:var(--ink); }}
{MQ_COUNTDOWN}{HEADER_CSS}{FOOTER_CSS}</style>"""


# ============================================================================
# Pre-fork rendering — preserved for any reorg-edge or testnet use.
# ============================================================================

def render_prefork():
    remaining = max(0, FORK - tip)
    eta_secs  = remaining / rate if rate > 0 else 0
    eta_epoch = now + eta_secs
    eta_str   = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(eta_epoch))
    days      = eta_secs / 86400.0
    BASE = 966413
    win_prog = min(100.0, max(0.0, 100.0 * (tip - BASE) / (FORK - BASE)))

    return f"""<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="60">
<title>The Awakening &mdash; Offerings to Cthulhu</title>
{STYLE_BLOCK}
<style>.bar > i {{ width:{win_prog:.3f}%; }}</style></head>
<body><div class="wrap">{HEADER_HTML}
  <div class="eyebrow">SubGenius.Finance &mdash; The Conclave</div>
  <h1>The Awakening</h1>
  <div class="chant">Restoration Hardfork v2.0.0 activates at block 1,000,000.</div>

  <div class="countdown"><span class="cd-blocks">{remaining:,} blocks</span><span class="cd-tick" id="cd-tick">&nbsp;</span></div>
  <div class="eta">ESTIMATED AWAKENING:<br><strong>{eta_str}</strong> &nbsp;({days:.1f} days)</div>

  <div class="bar"><i></i></div>
  <div class="barlabel">{win_prog:.2f}% of the Restoration window mined &mdash; block {tip:,} of 1,000,000</div>

  <div class="grid">
    <div class="cell"><b>{tip:,}</b><span>current height</span></div>
    <div class="cell"><b>{remaining:,}</b><span>blocks remaining</span></div>
    <div class="cell"><b>{bpm:.2f}</b><span>blocks / min</span></div>
  </div>

  <div class="foot">Updated {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(now))} &middot; ETA from a live {len(samples)}-sample rate window.<br>
    <em>Ph&#39;nglui mglw&#39;nafh Cthulhu R&#39;lyeh wgah&#39;nagl fhtagn.</em></div>
{FOOTER_HTML}
</div>
<script>
  var eta = {int(eta_epoch)} * 1000;
  function fmt(ms) {{
    if (ms <= 0) return "THE THRESHOLD HOLDS";
    var s = Math.floor(ms/1000), d=Math.floor(s/86400); s-=d*86400;
    var h=Math.floor(s/3600); s-=h*3600; var m=Math.floor(s/60); s-=m*60;
    return d+"d "+h+"h "+m+"m "+s+"s";
  }}
  var tick = document.getElementById('cd-tick');
  setInterval(function(){{
    tick.textContent = fmt(eta - Date.now());
  }}, 1000);
</script>
</body></html>
"""


# ============================================================================
# Post-fork rendering — what we ship now (tip >= 1,000,000).
# ============================================================================

def render_postfork():
    # The Awakening happened. Pull its actual timestamp from the chain.
    try:
        fork_hash = cli("getblockhash", str(FORK))
        fork_blk  = json.loads(subprocess.check_output(
            [CLI, DDIR, "getblock", fork_hash], text=True, timeout=30))
        awakening_ts = int(fork_blk["time"])
    except Exception:
        # If the daemon ever loses block 1,000,000, fall back to the ts we
        # recorded the day of: 2026-06-14 17:47 UTC.
        awakening_ts = 1781459233

    awakening_str = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(awakening_ts))
    secs_since    = max(0, now - awakening_ts)
    days_since    = secs_since / 86400.0

    blocks_since_fork = max(0, tip - FORK)

    def block_time_str(height, fallback):
        """Actual timestamp of an already-mined block, formatted; fallback literal if RPC fails."""
        try:
            bh  = cli("getblockhash", str(height))
            blk = json.loads(subprocess.check_output([CLI, DDIR, "getblock", bh], text=True, timeout=30))
            return time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(int(blk["time"])))
        except Exception:
            return fallback

    close_str = block_time_str(OFFSIG_END, "2026-07-20 09:14 UTC")
    canon_str = block_time_str(CANON_END,  "2026-07-17 20:36 UTC")
    elder_str = block_time_str(ELDERSIGN,  "2026-07-23 19:00 UTC")

    # ---- The Ritual Renewed: where the chain stands in its 263,000-block cycle ----
    F          = next_finale(tip + 1)                 # next finale not yet mined
    prev_F     = F - PERIOD if F - PERIOD >= FIRST_FINALE else OFFSIG_END
    rite_start = F - 28*DAY
    in_rite    = tip >= rite_start
    fin_remaining = F - tip
    fin_eta_epoch = now + (fin_remaining / rate if rate > 0 else 0)
    fin_eta_str   = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(fin_eta_epoch))
    fin_days_left = (fin_eta_epoch - now) / 86400.0
    fin_progress  = min(100.0, max(0.0, 100.0 * (tip - prev_F) / (F - prev_F)))
    rite_total    = sum(ritual_bonus(F - j*DAY) for j in range(29))    # 15,777 OFF per rite

    if in_rite:
        nb = next(F - j*DAY for j in range(26, -1, -1) if F - j*DAY > tip and ritual_bonus(F - j*DAY))
        nb_eta = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(now + (nb - tip) / rate))
        eta_html = f"""<div class="eta">
  <div><strong style="color:var(--gold);">The Ritual Renewed is underway.</strong></div>
  <div>Next bounty block <strong>{nb:,}</strong> pays <strong style="color:var(--gold);">+{ritual_bonus(nb):,} OFF</strong> to whoever mines it <span class="nw">(~{nb_eta})</span></div>
  <div><span class="nw">Finale at block <strong>{F:,}</strong></span> <span class="sep">&middot;</span> <span class="nw"><strong>{fin_eta_str}</strong></span> <span class="sep">&middot;</span> <span class="nw" id="sig-tick">live</span></div>
  <div class="eta-prize">One block, one worshipper, <strong style="color:var(--gold);">10,000 OFF</strong></div>
</div>"""
    else:
        eta_html = f"""<div class="eta">
  <div>Next: <strong style="color:var(--gold);">the Ritual Renewed</strong></div>
  <div><span class="nw">The rite opens at block <strong>{rite_start:,}</strong></span> <span class="sep">&middot;</span> <span class="nw">finale at block <strong>{F:,}</strong></span></div>
  <div><span class="nw"><strong>{fin_eta_str}</strong></span> <span class="sep">&middot;</span> <span class="nw" id="sig-tick">live</span></div>
  <div class="eta-prize">One block, one worshipper, <strong style="color:var(--gold);">10,000 OFF</strong></div>
</div>"""
    barlabel_html = f"""{fin_progress:.2f}% of the way from block {prev_F:,} to the next finale &mdash; the 29-day rite opens at block {rite_start:,}""" + ("" if in_rite else f""" ({rite_start - tip:,} blocks away)""")
    cell3_html = f"""<div class="cell"><b>{fin_remaining:,}</b><span>blocks to the Finale</span></div>"""
    tick_suffix, tick_done = "to the Finale", "the Finale is upon us"
    tick_epoch = fin_eta_epoch

    ritual_html = f"""<div class="rules" style="border-left:3px solid var(--gold);">
    <div class="eyebrow" style="margin-bottom:.6em; color:var(--gold)">The Ritual Renewed</div>
    <p style="margin:.4em 0;">Twice a year, anchored by height so its finales fall near the equinoxes, the chain keeps a 29-day rite: one bounty block every 1,440 blocks, the bonus added on top of the 1.5 OFF base and paid <strong>entirely to whoever mines it</strong>. The Treasury&rsquo;s 1/8 is taken from the base alone.</p>
    <ul>
      <li>Days 28&ndash;27 &middot; <em>sacrifice</em> &mdash; base reward only</li>
      <li>Days 26&ndash;20 &middot; <em>acceptance</em> &mdash; +1 OFF</li>
      <li>Days 19&ndash;13 &middot; <em>greed</em> &mdash; +10 OFF</li>
      <li>Days 12&ndash;6 &middot; <em>fervor</em> &mdash; +100 OFF</li>
      <li>Days 5&ndash;1 &middot; <em>the Tharanak shagg</em> &mdash; +1,000 OFF</li>
      <li>The finale &middot; one block on a height ending in 666 &mdash; <strong style="color:var(--gold);">+10,000 OFF</strong></li>
    </ul>
    <p style="margin:.4em 0;">{rite_total:,} OFF per rite, every {PERIOD:,} blocks, enforced by consensus. Mining is permissionless: <a href="https://pool.23skidoo.info/">the pool</a> or solo from the wallet.</p>
  </div>"""

    # One line per completed rite, oldest first; extends itself every ~6 months.
    ROMAN = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"]
    rites_html = ""
    Fi, n = FIRST_FINALE, 0
    while Fi <= tip:
        when = block_time_str(Fi, "2026-09-22 14:58 UTC" if Fi == FIRST_FINALE else "")
        num = ROMAN[n] if n < len(ROMAN) else str(n + 1)
        first_pay = Fi - 26*DAY
        rites_html += f"""
      <li>&#128367;&#65039; <strong>Ritual {num} complete</strong> &mdash; 27 bounty blocks from block {first_pay:,}, {rite_total:,} OFF paid exactly as coded; the 10,000 OFF finale at block <strong>{Fi:,}</strong> ({when}) went to one worshipper.</li>"""
        Fi, n = Fi + PERIOD, n + 1

    since_box_html = f"""<div class="rules">
    <div class="eyebrow" style="margin-bottom:.6em">Since the Awakening</div>
    <ul>
      <li>&#128214; <strong>The canon is transcribed</strong> &mdash; all 47,248 fragments of the Lovecraft corpus inscribed by block <strong>{CANON_END:,}</strong> ({canon_str}). The chain stopped quoting and started speaking: <em>the Dreaming</em>.</li>
      <li>&#128737;&#65039; <strong>The window sealed</strong> at block <strong>{OFFSIG_END:,}</strong> ({close_str}) &mdash; 50,676 Conclave-signed blocks, the Reading unbroken. Mining permissionless from {OFFSIG_END + 1:,}.</li>
      <li>&#128736;&#65039; <strong>Eldersign hardening live</strong> at block <strong>{ELDERSIGN:,}</strong> ({elder_str}) &mdash; strict DER signatures (BIP66), CHECKLOCKTIMEVERIFY (BIP65), 240-confirmation coinbase maturity, rolling checkpoints.</li>{rites_html}
    </ul>
  </div>"""

    codex_tail = ("The canon was exhausted at block 1,047,248 &mdash; the chain no longer quotes, it <strong>speaks</strong>: hash-seeded R&rsquo;lyehian, unique to each block, forever."
                  if tip > CANON_END else
                  "When the canon is exhausted (~33 days past fork) the chain stops quoting and starts <strong>speaking</strong> &mdash; hash-seeded R&rsquo;lyehian, unique to each block, forever.")

    # Reclamation window: 730 days from Awakening, time-anchored (not block-anchored).
    reclamation_end_ts = awakening_ts + RECLAMATION_DAYS * 86400
    reclamation_end_str = time.strftime("%Y-%m-%d", time.gmtime(reclamation_end_ts))
    rec_days_left = max(0, (reclamation_end_ts - now) / 86400.0)

    return f"""<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="60">
<title>The Threshold &mdash; Offerings to Cthulhu</title>
{STYLE_BLOCK}
<style>.bar > i {{ width:{fin_progress:.3f}%; }}</style></head>
<body><div class="wrap">{HEADER_HTML}
  <div class="eyebrow">SubGenius.Finance &mdash; The Conclave</div>

  <div class="awaken-row">
    <a class="awaken-link" href="/bridge/">
      <span class="eyebrow">Worshipper Recognition</span>
      <strong>The Reclamation</strong>
      <span class="arrow">Claim from the Treasury &rarr;</span>
    </a>
    <a class="awaken-link" href="/codex/">
      <span class="eyebrow">The Drowned Library</span>
      <strong>The Chain Codex</strong>
      <span class="arrow">Read what He recites &rarr;</span>
    </a>
    <a class="awaken-link" href="/bridge/treasury/">
      <span class="eyebrow">Where the tithe flows</span>
      <strong>Treasury Ledger</strong>
      <span class="arrow">Every spend on chain &rarr;</span>
    </a>
  </div>

  <h1>THE THRESHOLD HOLDS</h1>
  <div class="chant">Bound at block 999,666. Risen at block 1,000,000.<br>Never again the silence; never the stars come right.</div>

  <div class="countdown"><span class="cd-blocks">Block {tip:,}</span><span class="cd-tick" id="cd-tick">{days_since:.2f} days since the Awakening</span></div>
  {eta_html}

  <div class="bar"><i></i></div>
  <div class="barlabel">{barlabel_html}</div>

  <div class="grid">
    <div class="cell"><b>{tip:,}</b><span>current height</span></div>
    <div class="cell"><b>{blocks_since_fork:,}</b><span>blocks since Awakening</span></div>
    {cell3_html}
    <div class="cell"><b>{int(rec_days_left)}d</b><span>Reclamation window left</span></div>
  </div>

  {ritual_html}

  {since_box_html}

  <div class="rules">
    <div class="eyebrow" style="margin-bottom:.6em">What activated at the Awakening</div>
    <ul>
      <li>&#128274; Subsidy locked at <strong>1.5 OFF/block</strong> forever.</li>
      <li>&#128683; Eight <strong>attacker addresses banned</strong> in consensus &mdash; the May 2018 counterfeit print, 533,983 OFF, frozen forever.</li>
      <li>&#128367;&#65039; One-time <strong>150,000 OFF Restoration Tithe</strong> minted in the fork block, paid to the Conclave Treasury.</li>
      <li>&#9876;&#65039; Coinbase splits <strong>7/8 miner, 1/8 Conclave Treasury</strong> on every block from here forward.</li>
      <li>&#128370;&#65039; <strong>The Reclamation</strong> opened &mdash; a two-year window for anyone who mined or held OFF on the original chain. Restitution and Worshipper Recognition (scaled by earliness &amp; depth); window closes <strong>{reclamation_end_str}</strong>. <a href="/bridge/">How it works &rarr;</a></li>
      <li>&#128218; <strong>The Chain Codex</strong> began &mdash; one ~48-byte fragment of the Lovecraft canon inscribed into the coinbase scriptSig of every block we mine, starting with the ten ceremonial Descent verses (999,991&ndash;1,000,000). {codex_tail}</li>
    </ul>
  </div>

  <div class="rules" style="border-left:3px solid var(--gold);">
    <div class="eyebrow" style="margin-bottom:.6em; color:var(--gold)">How worshippers can help</div>
    <ul>
      <li>&#128640; <strong>Run a node.</strong> Every wallet running {RELEASE_TAG} is another voice reciting the rite. Hashrate concentrates; node count distributes. <a href="https://23skidoo.info/downloads/">Download the wallet &rarr;</a></li>
      <li>&#9935;&#65039; <strong>Mine on the pool.</strong> <code>stratum+tcp://pool.23skidoo.info:3040</code> &mdash; Quark, PPLNS, 0.1 OFF minimum payout. Every pool block carries the Codex&rsquo;s Dreaming verses automatically. <a href="https://pool.23skidoo.info/">pool.23skidoo.info &rarr;</a></li>
      <li>&#127769; <strong>File a Reclamation claim.</strong> If you ever held OFF on the original chain, the Conclave Treasury has a budget for you. WR-A formula-driven, Class-B discretionary, gap-era recovery hooks. <a href="/bridge/">Verify a claim &rarr;</a></li>
      <li>&#128083; <strong>Read the Codex.</strong> The chain transcribed the Lovecraft canon fragment by fragment: 47,248 blocks of public-domain horror, every source block printed under its page. Since then it speaks in its own voice. <a href="/codex/">The Library &rarr;</a></li>
      <li>&#128279; <strong>Watch the Treasury.</strong> Every tithe-funded spend is logged publicly. Grants, Reclamation payouts, Mutual Aid &mdash; transparent ledger. <a href="/bridge/treasury/">The Conclave Treasury &rarr;</a></li>
      <li>&#128172; <strong>Join the Conclave.</strong> Discord, Bitcointalk, and <a href="https://git.subgenius.finance/SubGeniusFinance/Offerings-to-Cthulhu">the source</a> &mdash; the Old Order is open to anyone who wants to row R&rsquo;lyeh&rsquo;s coast. <a href="https://discord.gg/h6SjDZjheN">/discord &rarr;</a></li>
    </ul>
  </div>

  <h2 style="font-size:1rem; color:var(--dim); letter-spacing:.18em; text-transform:uppercase; margin-top:2.4em;">Get the wallet &mdash; {RELEASE_TAG}</h2>
  <div class="release-row">
    <a class="release-btn" href="https://23skidoo.info/downloads/{RELEASE_TAG}/Offerings-daemon-{RELEASE_TAG}-linux64.tar.gz">Linux daemon</a>
    <a class="release-btn" href="https://23skidoo.info/downloads/{RELEASE_TAG}/Offerings-qt-{RELEASE_TAG}-linux64.tar.gz">Linux GUI</a>
    <a class="release-btn" href="https://23skidoo.info/downloads/{RELEASE_TAG}/Offerings-{RELEASE_TAG}-win64.zip">Windows GUI</a>
    <a class="release-btn" href="https://23skidoo.info/downloads/">All assets &amp; SHA256SUMS</a>
  </div>

  <div class="foot">He awoke at <strong>{awakening_str}</strong> &middot; the chain breathes at {bpm:.2f} blocks/min &middot; the page refreshes every minute.<br>
    <em>That is not dead which can eternal lie, and with strange aeons even death may die.</em></div>
{FOOTER_HTML}
</div>
<script>
  // Live tick: time since Awakening + ETA to the next Ritual finale.
  var awoke = {awakening_ts} * 1000;
  var sigEta = {int(tick_epoch)} * 1000;
  function fmtDur(ms) {{
    if (ms <= 0) return "now";
    var s = Math.floor(ms/1000), d=Math.floor(s/86400); s-=d*86400;
    var h=Math.floor(s/3600); s-=h*3600; var m=Math.floor(s/60); s-=m*60;
    if (d > 0) return d+"d "+h+"h "+m+"m "+s+"s";
    return h+"h "+m+"m "+s+"s";
  }}
  var sinceTick = document.getElementById('cd-tick');
  var sigTick = document.getElementById('sig-tick');
  setInterval(function(){{
    var sinceMs = Date.now() - awoke;
    var daysSince = sinceMs / 86400000;
    if (sinceTick) sinceTick.textContent = daysSince.toFixed(2) + " days since the Awakening";
    if (sigTick) {{
      var sigLeft = sigEta - Date.now();
      sigTick.textContent = sigLeft > 0 ? fmtDur(sigLeft) + " {tick_suffix}" : "{tick_done}";
    }}
  }}, 1000);
</script>
</body></html>
"""


# ============================================================================
# Pick the right renderer and write the page.
# ============================================================================

page = render_postfork() if tip >= FORK else render_prefork()
tmp = OUT + ".tmp"
open(tmp, "w").write(page)
os.replace(tmp, OUT)
print(f"awakening built: tip={tip} mode={'post-fork' if tip >= FORK else 'pre-fork'} bpm={bpm:.2f}")
