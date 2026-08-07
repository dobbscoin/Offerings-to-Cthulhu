# OFF pool Stratum v1 dialect — live probe, 2026-08-07

Read-only probe of `stratum+tcp://pool.23skidoo.info:3040` (Miningcore, pool id
`offerings`): TCP connect → `mining.subscribe` → `mining.authorize` → captured
~70 s of raw traffic (three block rotations at the 60 s target). No shares
submitted. This pins the wire format for the in-wallet Stratum client (issue #8
pool mode) so `src/stratum.cpp` is written against ground truth, not guesses.

**Headline: the dialect is 100% conventional Miningcore Stratum v1.** No Quark
weirdness on the wire. The one remaining chain-specific unknown is the
share-difficulty→target multiplier used by the pool's Quark module (see § Open
items).

## Subscribe / authorize

```
>>> {"id":1,"method":"mining.subscribe","params":["stratum-probe/0.1"]}
<<< {"result":[[["mining.set_difficulty","0HNNH1MKCD5RE"],["mining.notify","0HNNH1MKCD5RE"]],
     "c0000027",4],"error":null,"id":1}
<<< {"jsonrpc":"2.0","method":"mining.set_difficulty","params":[0.001],"id":null}
>>> {"id":2,"method":"mining.authorize","params":["<Q-address>","x"]}
<<< {"result":true,"error":null,"id":2}
```

- extranonce1 = `c0000027` (4 bytes hex), **extranonce2_size = 4** → 8 bytes
  total spliced between coinb1/coinb2.
- Initial vardiff for a fresh connection: **0.001** (CPU-friendly; the pool's
  configured miner floor may ramp it via later `mining.set_difficulty`).
- Authorize: username = bare Q-address, password ignored — matches the pool's
  published convention.

## mining.notify

Standard 9-param layout, one job every ~10 s, `clean=true` on each new block:

```
params = [jobId, prevHashStratum, coinb1, coinb2, merkleBranch[],
          version, nBits, nTime, cleanJobs]
```

Sample (job `0000a5dc`, height 1,075,893):

| field | value | decoded |
|---|---|---|
| jobId | `0000a5dc` | opaque, echo back in submit |
| prevHash | `54cbcb34…0000005f` | see § Byte order — dword-reversed RPC hash |
| coinb1 | `01000000…ffffffff53 03b56a10 08` | txver 1, null prevout, scriptSig len 0x53, BIP34 height push `03 b5 6a 10` = 1,075,893, then `08` = push for the 8 extranonce bytes |
| coinb2 | `38 4f464631 feffffff <verse> 0c /Miningcore/ ffffffff 02 …` | see below |
| merkleBranch | `[]` | empty mempool at capture; hashes to fold pairwise when present |
| version | `00000070` | big-endian hex of the template's int32 block version |
| nBits | `1e00b13a` | compact target, use as-is for block validity |
| nTime | `6a75dc06` | big-endian hex epoch (2026-08-07), advances ~10 s per job |
| clean | true/false | true on new-block rotation |

**coinb2 decode — the wire carries the whole Restoration.** The scriptSig tail
is a 56-byte push: `OFF1` magic + chunk index `0xFFFFFFFE` (CODEX_DREAMING) +
the current R'lyehian verse (rotates per block: "Gof'nn ng n'gha goka
vulgtlagln ehye uaaah uaaah", "Fhtagn ooboshu syha'h gnaiih ep nog wgah'nagl",
"Nog syha'h lloig Cthulhu ee n'gha ilyaa ng" across the three captured
heights), then `0c` `/Miningcore/`. Outputs, exactly two:

- `b2000200 00000000` = 131,250 atoms = **1.3125 OFF → miner P2PKH** (pool payout key)
- `3e490000 00000000` = 18,750 atoms = **0.1875 OFF → Treasury P2SH `1bb03a89…6094b686`** (Conclave 2-of-3)

i.e. the 7/8 : 1/8 Restoration split and the Dreaming inscription are baked
into every job template (the BuildPoolCoinbase contract). The in-wallet client
never constructs these — it just splices extranonce and hashes.

## Byte order (the feared risk — resolved)

Stratum prevHash `54cbcb3456f5818bb7f1283341088b7c6a2c289eab976f3d5384816e0000005f`
vs `getblockhash 1075892` = `0000005f5384816eab976f3d6a2c289e41088b7cb7f1283356f5818b54cbcb34`:

**The transform is dword-order reversal only** — split the RPC hash into 8×4-byte
words, reverse the word order, leave bytes within each word untouched. Verified
exact against the explorer. This is stock Miningcore behavior; no Quark-specific
munging on the wire.

Header assembly for hashing (80 bytes, standard bitcoin layout):
`version(LE) ‖ prevhash(32B, RPC hash byte-reversed) ‖ merkleroot(32B, LE) ‖
nTime(LE) ‖ nBits(LE) ‖ nonce(LE)`, then Quark Hash9 (`src/quark.cpp`) over the
80 bytes. Coinbase txid = double-SHA256 of `coinb1‖extranonce1‖extranonce2‖coinb2`,
folded through merkleBranch pairwise (double-SHA256, branch hash appended).

## Open items

1. **Share target multiplier.** `mining.set_difficulty 0.001` → target depends
   on the diff-1 constant the pool's Quark module uses (Bitcoin's `1d00ffff`
   vs the `1e0fffff`-style powLimit common to Quark chains). Don't guess:
   the pool's Miningcore build (and its `Quark.cs`) is on the pool host —
   read `GetShareDifficulty`/multiplier there before writing share-accept
   logic, or determine empirically from the first accepted/rejected share.
2. **`version` field semantics** — `00000070` should be confirmed against a
   recent pool-mined block's `getblock <hash> .version` before submit code
   hardcodes an interpretation.
3. **mining.submit** param order untested (standard is
   `[user, jobId, extranonce2, nTime, nonce]`) — verify against Miningcore
   source before first submit.

Probe script preserved inline in this doc's git history context; re-run =
15 lines of python (subscribe, authorize, log).
