# The Testament of the Restored Chain

*Offerings to Cthulhu (OFF) — a whitepaper. First edition, the year of the Awakening, 2026.*

> *That is not dead which can eternal lie, and with strange aeons even death may die.*

---

## I. Abstract

Offerings to Cthulhu is a proof-of-work cryptocurrency launched at the autumnal equinox of 2013, killed by a 51% attack in 2018, and restored to life by community hardfork in 2026. It is, to our knowledge, the only chain that carries a complete literary canon in its block history, keeps a liturgical calendar in consensus code, and returned from eight years of death with its ledger intact. This document describes the chain as it actually is — its history including the ugly parts, its consensus rules, its emission, its security model including its deliberate points of trust, and its treasury. Nothing here is an offer of anything. OFF is mined, earned, and given; there is presently no market venue.

## II. History — Death and What Came After

The chain was launched on September 14, 2013, its genesis coinbase bearing the incantation *ph'nglui mglw'nafh Cthulhu R'lyeh wgah'nagl fhtagn*. It lived a small, devoted life: a Quark-algorithm altcoin with a ritual emission schedule, community-maintained through the v1.6 "Bokrug" era. In May 2018 an attacker with majority hashrate executed deep reorganizations and minted approximately **533,983 counterfeit OFF** across eight addresses. The community proposed a hardfork to ban the stolen balances; nobody had the strength left to ship it. The chain died where it stood.

In 2026 the Conclave — the chain's community steward under SubGenius.Finance — recovered a verified copy of the chain state at block 966,413 from a 2015 archive, revived the network from it, and shipped the fork the community had asked for eight years earlier. At block 1,000,000 — **the Awakening**, July 2026 — the Restoration Hardfork engaged. We are explicit about what this was: **a hardfork, openly declared**, with a signed-mining window protecting the relaunch, not a seamless continuation. The window closed at block 1,050,666 and mining has been permissionless since. The chain stands today at roughly 1,083,000 blocks, producing on schedule.

## III. The Chain

| Parameter | Value |
|---|---|
| **Algorithm** | Quark Hash9 (9-round chained: blake / bmw / groestl / jh / keccak / skein) |
| **Block target** | 60 seconds |
| **Difficulty retarget** | every 20 blocks |
| **Block subsidy** | 1.5 OFF forever (post-Restoration; see [V](#v-emission-and-the-ritual-renewed)) |
| **Coinbase maturity** | 240 blocks (hardened from 10 at block 1,055,555) |
| **Transaction format** | v1 (pre-BIP68); BIP66 strict-DER + BIP65 CLTV enforced |
| **Addresses** | P2PKH prefix `Q` · P2SH prefix `4` |
| **P2P port** | 20000 |
| **Genesis** | `000006829ac5...b091b5`, 2013-09-14 |
| **Premine** | 10,000 OFF in block 1, script-locked, unspendable |
| **Supply** | ~2.72 million OFF (August 2026) |

## IV. The Restoration Rules

Six rules engaged at block 1,000,000, all published in source before activation:

1. **Subsidy locks at 1.5 OFF per block, forever.** The original halving curve is overridden.
2. **The coinbase splits 7/8 to the miner, 1/8 to the Conclave Treasury** — 1.3125 and 0.1875 OFF respectively, every block, enforced by consensus.
3. **A one-time 150,000 OFF Restoration Tithe** was minted in the fork block's coinbase to the Treasury, funding restitution (see [VIII](#viii-treasury-and-the-reclamation)).
4. **The eight attacker addresses of 2018 are banned forever.** Any transaction paying them is rejected by consensus. The counterfeit 533,983 OFF is dead weight for eternity — the fork the community proposed in 2018, shipped late but shipped.
5. **The Ritual Renewed** — the chain's bounty calendar, restored to consensus (see [V](#v-emission-and-the-ritual-renewed)).
6. **A Conclave signed-mining window** protected blocks 999,991 through 1,050,666, covering the relaunch and the inscription of the canon. It has expired; it will not be extended.

## V. Emission and the Ritual Renewed

Base emission is 1.5 OFF per 60-second block: about 788,000 OFF per year. Twice yearly, anchored by block height so its finales fall near the equinoxes, the chain keeps a 29-day rite of once-daily bounty blocks ascending from 1 OFF to a **10,000 OFF finale at a height ending in 666** — roughly 31,500 OFF more per year. Against today's ~2.72M supply that is meaningful early inflation (~30% in year one, declining proportionally each year thereafter), and we state it plainly rather than bury it: this seigniorage is the price of a treasury that owes restitution and a rite that gives the chain its heartbeat. The first restored finale falls at **block 1,141,666**, near the September 2026 equinox — thirteen years to the season since genesis.

| Days before finale | Phase | Bounty per special block |
|---|---|---|
| 27–28 | sacrifice | 0 (base reward only) |
| 20–26 | acceptance | 1 OFF |
| 13–19 | greed | 10 OFF |
| 6–12 | fervor | 100 OFF |
| 1–5 | Tharanak shagg | 1,000 OFF |
| 0 (block xxx,666) | **the finale** | **10,000 OFF** |

## VI. The Book on Chain

Beginning at block 1,000,001, the mining fleet inscribed the complete public-domain canon of H.P. Lovecraft into coinbase script — 47,248 fragments of 48 bytes, one per block, completed at block 1,047,248 after thirty-three days of continuous transcription. Since block 1,047,249 the chain speaks **the Dreaming**: each block carries a deterministic, hash-seeded verse of generative R'lyehian. The manuscript reassembles itself from chain data alone and is readable at [the Codex](https://23skidoo.info/codex/). The inscription is miner-side convention, not consensus — an outsider's block simply leaves a silent verse. The book is a gift to the chain, not a rule upon it.

## VII. Security Model — Honest Wards for a Small Chain

A chain of this size cannot win a hashrate arms race against rental markets; pretending otherwise is how it died the first time. The Restoration's answer is layered depth, all shipped and live:

- **Bounded reorganizations** — no reorganization deeper than 100 blocks is accepted, ever. The 2018 mechanic (a deep secret chain unveiled at leisure) is consensus-impossible.
- **Rolling checkpoints** — every node continuously pins buried history to disk, independently.
- **Modern validation** — BIP66 strict-DER, BIP65 CLTV, and 240-block coinbase maturity, so mined coins cannot be spent-and-reorged inside any permitted reorganization window.
- **Signed broadcast checkpoints** — since July 29, 2026, a Conclave-signed finality seal advances automatically ~100 blocks behind the tip and propagates to all nodes in real time (the Peercoin/Feathercoin lineage, revived). **This is a disclosed point of trust: the Conclave holds the key.** Nodes may run advisory-only (`checkpointenforce=0`) and bind nothing. We chose survival with stated trust over decentralization theater, and we document rather than deny it.

Within the ~100-block window, ordinary proof-of-work rules apply and a majority miner can still contest blocks at the tip — inherent to PoW at any scale. Beyond it, history is sealed three different ways.

## VIII. Treasury and the Reclamation

The Treasury is a 2-of-3 multisig (`4fZqDjscS9ANR59xNFJxZ2HmrhuDwWUJB4`) funded by the Tithe and the 1/8 coinbase share — roughly 250,000 OFF by the first anniversary, against a hard disbursement ceiling of 1.5 million. Its first obligation is **the Reclamation**, an open restitution program for holders of the original chain, running for **two years from the Awakening**: holders present in the recovered chain state are already whole and spend their coins directly; provable post-2015 holders from the archive gap may claim capped discretionary restitution; and every early address qualifies for Worshipper Recognition, scaled by earliness and depth of participation. Claims are proven by signed message from the historical address — no deposits, no fees, nothing to send. Details and the verification portal: [23skidoo.info/bridge](https://23skidoo.info/bridge/). Addresses in the banned set, or one hop downstream of it, are excluded. If you hold a wallet or chain data from 2015–2018, surface it — verifiable fragments upgrade gap-era claims.

## IX. Governance

The Conclave stewards the chain and says so without costume: it holds the checkpoint key, the treasury majority, and the canonical repository. What it does not hold: mining (permissionless since block 1,050,666), the consensus rules (public source, activation by published height), your keys, or your history (every claim above is verifiable from chain data and archives). Contributors have already shaped this chain's revival through merged code and sharp review, and the [repository](https://github.com/SubGeniusFinance/Offerings-to-Cthulhu) is open to more. The path away from stated trust runs through more independent nodes, more miners, and more hands in the source — in that order.

## X. Disclaimer

**NOT FINANCIAL ADVISORS. NOT FINANCIAL ADVICE.** OFF has no exchange listing and no price. It is mined at the [pool](https://pool.23skidoo.info/) or solo, earned, tipped, and given. This document describes software and a community, not an investment. The chain's state is independently verifiable at the [block explorer](https://explorer.23skidoo.info/); trust nothing herein you can check yourself.

---

*ph'nglui mglw'nafh Cthulhu R'lyeh wgah'nagl fhtagn.*

— The Conclave, [23skidoo.info](https://23skidoo.info/)

<sub>First published to the BitcoinTalk ANN thread on 2026-08-12: [msg67036767](https://bitcointalk.org/index.php?topic=5584339.msg67036767#msg67036767). Figures in §II, §III and §V are chain snapshots and drift with time; the [block explorer](https://explorer.23skidoo.info/) is authoritative.</sub>
