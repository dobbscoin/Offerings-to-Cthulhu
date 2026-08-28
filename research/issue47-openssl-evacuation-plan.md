# Issue #47 — OpenSSL Evacuation: Validated Implementation Plan

*Drafted 2026-07-30 from a full-tree dependency survey. Supersedes the
seven-subtask sketch in the issue body where they differ — the survey
found two subsystems the issue missed entirely (native hashing and
CBigNum), one of which is the hardest item in the whole evacuation.*

## Ground truth (full-tree survey, 2026-07-30)

Everything the issue body claimed checks out, plus these deltas:

1. **There is no `src/crypto/`.** Consensus hashing — txid double-SHA256,
   `Hash160`, `OP_SHA256`/`OP_HASH160`, BIP32 HMAC-SHA512 — is 100%
   OpenSSL (`hash.h`/`hash.cpp` on `SHA256_*`/`RIPEMD160`/`SHA512_*`).
   Upstream added native `src/crypto/{sha256,sha512,ripemd160,hmac_sha512}`
   in 0.11; this fork never received it. The issue's subtask list omits
   this entirely. Mechanical + bit-identical, but it's a real subtask.
2. **CBigNum is not a "payoff commit" cleanup — it's the hardest item.**
   `bignum.h` declares `class CBigNum : public BIGNUM` (inheriting the
   OpenSSL struct — impossible on OpenSSL 1.1+/3.x where BIGNUM is
   opaque), and its consumers are the script interpreter's arithmetic
   opcodes (`script.cpp` OP_ADD/OP_WITHIN/…), PoW target math
   (`SetCompact`/`GetCompact`, `pow.cpp` retarget, `CheckProofOfWork`),
   and chain-work accumulation (`GetBlockWork`). Upstream needed two
   separate migrations: `arith_uint256` (0.10/0.11) for work/target and
   `CScriptNum` (PR #4988) for script — the latter with a careful
   ≤4-byte-operand equivalence argument. Wrinkle: `pow.cpp:16` says the
   LWMA-3 work *back-ported 0.10 arith_uint256-idiom code into CBigNum* —
   that goes the wrong direction and must be unwound.
3. **No vendored libsecp256k1 anywhere** — no `src/secp256k1/`, no
   `USE_SECP256K1` gates. Both sign AND verify run through OpenSSL
   `EC_KEY`, including the hand-rolled `EC_KEY_regenerate_key` and
   `ECDSA_SIG_recover_key_GFp` (SEC1 pubkey recovery) with direct
   `sig->r`/`sig->s` struct access (opaque on 1.1+).
4. **The OpenSSL link is invisible at the Makefile level** —
   `configure.ac` injects `-lssl -lcrypto` into global `LIBS` (fatal
   checks at configure.ac:527-540), so every binary links both and no
   `_LDADD` edit will show it.
5. Confirmed clean: the Quark PoW (`hashblock.h` + `sph_*` files) uses
   zero OpenSSL. `src/db.cpp`'s `<openssl/rand.h>` include is dead code.
6. The tree currently compiles against a hand-built OpenSSL 1.0.2
   (visible in `.deps`), confirming it cannot build against distro
   OpenSSL 1.1/3 today.

## Donor tree: dobbscoin-source (added same day)

The sibling project's tree (git.subgenius.finance/SubGeniusFinance/dobbscoin-source,
a genuine upstream Bitcoin Core 0.10 layout, **already modernized to
build against OpenSSL 3** in its v0.10.3/v0.10.4 releases) contains
working, maintainer-owned implementations of most of what this plan
ports:

| OFF phase | Donor artifact | Notes |
|---|---|---|
| Phase 3 (hashing) | `src/crypto/{sha256,sha512,ripemd160,hmac_sha256,hmac_sha512}` | lift directly |
| Phase 4a (vendor) | `src/secp256k1/` vendored subtree + autotools wiring | proven build integration |
| Phase 4c (sign) | `src/key.cpp` — sign path already on `secp256k1_ecdsa_sign` (0.10 shape) | |
| Phase 4b interim | `src/ecwrapper.{h,cpp}` — OpenSSL verify + SEC1 pubkey recovery in **OpenSSL-3-compatible opaque-struct form** | verify stays OpenSSL but unpins 1.0.2 |
| Phase 5b (script) | `src/script/interpreter.cpp` + `script.h` `CScriptNum`, plus `src/test/scriptnum_tests.cpp` **and the test-only `src/test/bignum.h`** (upstream's reference implementation used to prove CScriptNum equivalence) | the equivalence harness comes free |
| Phase 5a alternative | `src/pow.cpp` `BignumPtr` wrappers — opaque-safe BIGNUM helpers that let KGW/DigiShield bignum math build on OpenSSL 3 | cheap unpin path, see below |
| Phase 2 (crypter) | `src/wallet/crypter.cpp` — same `EVP_BytesToKey` KDF, `EVP_CIPHER_CTX_new/free` heap style (1.1+/3-safe) | KDF compat proven |

**Strategic consequence — a new intermediate milestone.** dobbscoin
proves that "builds against OpenSSL 3" and "OpenSSL-free" are separable
goals. OFF's acute pain is the 1.0.2 pin, and that can die much earlier
than full evacuation by porting dobbscoin's compat patterns
(opaque-struct fixes: `ECDSA_SIG` accessors, heap `EVP_CIPHER_CTX`,
`BignumPtr`-style bignum wrappers, ecwrapper verify). Full evacuation
(no libcrypto at all) then proceeds on the original phases at leisure.

**Porting caveat:** OFF's interpreter is 0.8-lineage — dobbscoin's 0.10
`script/interpreter.cpp` cannot be lifted wholesale. Port the
`CScriptNum` semantics *into* OFF's interpreter, and replay the
equivalence argument against OFF's script usage; the donor's
`scriptnum_tests` + test-only bignum.h make that tractable.

## Phased plan (cheapest → most hazardous)

Each consensus phase gets its own release + soak; never bundle two
consensus phases, and never bundle a consensus phase with other
consensus work (per the issue's own sequencing rule).

### Phase 0 — trivia (any release, zero risk)
- Delete `db.cpp`'s dead `<openssl/rand.h>` include.
- Replace/drop `SSLeay_version` banners (`init.cpp`, `qt/rpcconsole.cpp`
  + the `.ui` widget) — the symbol is gone in OpenSSL 3 anyway.

### Phase 1 — deletions, no replacement code (non-consensus)
- **Drop `-rpcssl`** (`rpcserver.cpp` ssl::context, `rpcclient.cpp`,
  `rpcprotocol.{h,cpp}` `SSLIOStreamDevice`, init.cpp help). Upstream
  deleted it in 0.12; stunnel/SSH is the answer. **This removes the
  entire `libssl` half of the link.** Keep the plaintext boost::asio
  path — no libevent migration needed.
- **Drop BIP70/paymentserver** (`qt/paymentserver.*`,
  `qt/paymentrequestplus.*`, `qt/test/paymentservertests.cpp`) — the
  only X509/EVP_Verify user — and with it the **protobuf** build dep
  (`src/qt/Makefile.am`). Upstream removed BIP70 by 0.20.

### Phase 2 — non-consensus replacements
- **RNG:** port upstream `random.cpp` (`GetRandBytes`/`GetStrongRandBytes`).
  Replaces `RAND_bytes`/`RAND_add` across `util.cpp`, `net.cpp`,
  `addrman.h`, `wallet.cpp`, `key.cpp:363`, and deletes the
  `CRYPTO_set_locking_callback` block (`util.cpp:118-140`) — which is
  itself a compile error on OpenSSL 1.1+.
- **`OPENSSL_cleanse` → `memory_cleanse`** (`support/cleanse.cpp` port;
  `allocators.h`, `crypter`, `util.cpp`).
- **Wallet AES → ctaes** (`crypter.cpp`): port upstream `crypto/aes.cpp`.
  **Gate: the `EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha512(), …)` KDF
  must be reimplemented bit-exactly** or every existing encrypted
  wallet.dat becomes unreadable. Acceptance test = lock/unlock/spend
  round-trip on a pre-change encrypted wallet.dat (issue's own gate).

### Phase 3 — consensus-mechanical: native hashing
- Port `src/crypto/{sha256,sha512,ripemd160,hmac_sha512}` (0.11-era,
  plus 0.13 SHA512 for HMAC) and rewrite `hash.{h,cpp}` on them.
  Bit-identical output ⇒ no consensus change *if correct*; treat as
  consensus-grade anyway. Tests: known-answer vectors + full unit suite
  + reindex-from-zero spot check (identical tip + chainstate hash).
- This is also what unblocks `key.h`/`script.h`/`main.h` from
  transitively including `<openssl/*.h>`.

### Phase 4 — consensus EC: libsecp256k1
- **4a. Vendor** `src/secp256k1/` at the 0.12/0.13-era pin, wire into
  autotools.
- **4b. Verify swap** (`key.cpp` → `secp256k1_ecdsa_verify` +
  `_recover`). The consensus-sensitive step. Landmines from the issue
  hold: (A) buried pre-checkpoint non-strict-DER sigs — mitigation is
  reindex-from-zero validation, with an OpenSSL fallback gated below the
  BIP66 height (h=1,055,555) as plan-B; (B) low-S stays policy-only.
  Prerequisite (#33 active on mainnet) is **already satisfied** —
  activated cleanly 2026-07-23.
- **4c. Sign swap** — not consensus-critical; free RFC6979 deterministic
  nonces. Also retires `canonical_tests.cpp`'s OpenSSL-as-DER-oracle.
- 4b and 4c can ship together (one release), after Phase 3 has soaked.

### Phase 5 — consensus-hazardous: CBigNum retirement
- **5a. `arith_uint256`** for target/work math (`SetCompact`/`GetCompact`,
  `pow.cpp`, `GetBlockWork`, `chainparams` PoW limits, `ComputeMinWork`),
  unwinding the pow.cpp back-port noted above. Equivalence testable
  exhaustively over the compact-encoding domain.
- **5b. `CScriptNum`** for the script interpreter (upstream PR #4988).
  The single riskiest change in the whole evacuation — needs the
  ≤4-byte operand equivalence argument replayed against OFF's script
  usage plus the full script test vectors, and its own soak.
- After 5b, `bignum.h` deletes; `txdb`'s `WriteBestInvalidWork` (local
  db only) migrates or drops with it.

### Payoff commit
Strip `configure.ac:527-540` + the macOS brew hack, drop OpenSSL from
`depends/`, delete the OpenSSL-1.0.2 build recipe from `doc/build-unix.md`.
Neither binary links `libssl` or `libcrypto`. The 1.0.2 pin is dead.

## Release-vehicle mapping (revised for the donor tree)

| Release | Content | Consensus surface |
|---|---|---|
| v2.1.1 (whenever cut) | Phases 0-1 (+#50/#51 already merged) | none |
| v2.1.2 | Phase 2 (RNG, cleanse, crypter heap-CTX) **+ M1: OpenSSL-3 compat unpin** — port dobbscoin's opaque-struct fixes (key.cpp/ecwrapper pattern, BignumPtr pow wrappers, heap EVP_CIPHER_CTX). **Retires the 1.0.2 pin** while still linking (modern) OpenSSL. | none if bignum wrappers are value-identical (testable) |
| v2.1.3 | Phase 3 (native hashing, from donor src/crypto/) | bit-identical; soak anyway |
| v2.2.0 | Phase 4 (vendor secp256k1 + sign; verify swap) | YES — own soak |
| v2.3.0 | Phase 5 (native work/target math + CScriptNum via donor harness) + payoff unlink | YES — own soak |

Phases 0-2 + M1 are safe to start immediately. Phase 5b should never
share a release with anything else. M1 changes the payoff curve
dramatically: the build-pain motivation is satisfied at v2.1.2 instead
of v2.3.0; everything after is security/consensus-hygiene driven.

## Test plan (cumulative)

- Full unit suite + regtest harnesses per phase.
- Phase 2: encrypted-wallet lock/unlock/spend round-trip on a
  pre-change wallet.dat.
- Phase 3: hash known-answer vectors; reindex-from-zero, assert
  identical tip hash + `gettxoutsetinfo` hash vs a pre-change node.
- Phase 4: reindex-from-zero (landmine A); cross-version testnet soak
  with mixed old/new verify across fresh blocks; sign/verify round-trip
  + RFC6979 determinism vectors.
- Phase 5: script test vectors (port upstream's script_tests JSON for
  #4988 coverage); exhaustive compact-encoding equivalence for 5a;
  cross-version soak.
