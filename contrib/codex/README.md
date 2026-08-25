# OFF Chain Codex — website build pipeline

This directory holds the build scripts and frozen assets that drive the public-facing
Codex of the Drowned reader at `https://23skidoo.info/codex/` and the Awakening
countdown at `https://23skidoo.info/awakening/`. Scripts run on cron on the production
host against the local OFF daemon's RPC.

## Layout

| File | What it is |
|---|---|
| `build_ereader.py` | Per-book paginated reader. Reveals the 23 Lovecraft works as the chain inscribes them; the Proem (book 0) is always open. Writes `/var/www/23skidoo.info/codex/book_*.html` + `/codex/tts-pages.json`. |
| `build_codex.py` | Legacy contiguous-chunk decoder; writes `/var/www/23skidoo.info/codex/index.html` if you bypass the e-reader's own index. Not currently in cron. |
| `build_countdown.py` | Renders the fork-block countdown at `/awakening/`. Flips to "AWAKENED" at height ≥ 1,000,000. |
| `build_tts.py` | Nightly ElevenLabs TTS renderer. Reads `tts-pages.json`, fills missing `/static/tts/{key}.mp3` files within monthly budget. Content-addressed cache (`sha256(page_text)[:16]`). |
| `codex_corpus.txt` | The 24-work corpus, frozen at fork. 0x1E-separated records. SHA-256 `1c808560eebab87f9e8c5032e5157821f1fbdae2bafc6ea68b6f4310a2d6d82f`. |
| `codex_manifest.json` | Book index (title + body_start + body_len). Pairs with the corpus. |
| `post-fork-backlog.md` | Post-fork to-do list. |
| `courtesy-pr-draft.md` | Notes for courtesy GH PRs to JimGilmore/CatmanIX/etc. once the upstream chain takeover lands. |

## Path conventions

Scripts split paths into two namespaces:

- **`SCRIPT_DIR`** (where this README lives) — frozen, version-controlled assets:
  the corpus, the manifest, the scripts themselves. Resolved via `__file__` so the
  scripts run correctly no matter what cwd you invoke them from.
- **`STATE_DIR = /home/btcbob/codex`** — runtime state on the production host:
  - `*_cache.json` — incremental scan checkpoints (one per script)
  - `*_cron.log` / `cron.log` — cron stderr/stdout captures
  - `tts_state.json` — monthly char-budget ledger
  - `eleven.env` — ElevenLabs API key (mode 0600, NEVER commit)

The state dir is intentionally outside the repo so caches don't churn the git tree
and secrets stay off GitHub.

## Cron entries (production host, `crontab -e`)

```
* * * * *  /usr/bin/python3 /home/btcbob/claude/offerings-master/contrib/codex/build_countdown.py >/home/btcbob/codex/countdown_cron.log 2>&1
*/2 * * * * /usr/bin/python3 /home/btcbob/claude/offerings-master/contrib/codex/build_ereader.py  >/home/btcbob/codex/ereader_cron.log 2>&1
0 4 * * *  /usr/bin/python3 /home/btcbob/claude/offerings-master/contrib/codex/build_tts.py      >>/home/btcbob/codex/tts_cron.log 2>&1
```

## Deploy / change procedure

1. Edit the script here in the repo (`~/claude/offerings-master/contrib/codex/`)
2. Test by running the script manually from any cwd — it should still find the corpus
3. `git add` + commit + push to `dobbscoin/Offerings-to-Cthulhu`
4. Crons pick up the change on the next minute boundary

No deploy step needed — the scripts run in-place from the canonical repo.
