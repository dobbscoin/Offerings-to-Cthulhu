# Phase-2 ACP — Operator Runbook

> Mainnet first-light + steady-state operation of the broadcast-checkpoint
> overlay (issue #40). Read `AUDIT.md` in this directory for the underlying
> mechanism map. **No consensus change required** — this is an ops procedure.

## Audience

The Conclave operator who holds the privkey for Conclave Key #1
(`vConclaveKeys[0]`, P2PKH `Qcad56y76jCGgjkhGHPyCMY89uC9j9CBXC`,
pubkey hex `0238efde…46b111`). Only this operator can perform broadcaster
first-light. Recipient nodes need only the Phase-2-aware build.

## Prerequisites

- Daemon built from a branch with #40's `checkpoints.cpp` refactor — confirm
  with `grep -q "GetCheckpointMasterPubKeyHex" Offeringsd` (or by SHA of the
  binary, once a tagged release ships).
- Operator's wallet contains the Conclave Key #1 privkey. Verify:
  ```bash
  Offerings-cli validateaddress Qcad56y76jCGgjkhGHPyCMY89uC9j9CBXC \
      | grep -E '"ismine"\s*:\s*true'
  ```
  Empty output ⇒ wrong wallet; abort.
- Daemon is fully synced to tip. Verify `getblockcount` matches a known
  public explorer's height ± 1.
- Recent `wallet.dat` backup exists. The privkey is irreplaceable — losing it
  forecloses on Slot #1 forever. Treasury redundancy still spans Slots #2
  and #3, but Slot #1 cannot be regenerated.

## Pre-deployment checks

```bash
# 1. Tip + ACP state (expected: hashSyncCheckpoint = genesis hash, the inert default)
Offerings-cli getblockcount
Offerings-cli getinfo | grep -i checkpoint

# 2. Daemon version sanity — Phase-2-aware build has the new helper symbol.
#    The pre-#40 build silently uses the stale ppcoin pubkey and will not
#    accept anything we sign.
strings $(which Offeringsd) | grep GetCheckpointMasterPubKeyHex
# expected: one match

# 3. Wallet ownership of Conclave Key #1
Offerings-cli validateaddress Qcad56y76jCGgjkhGHPyCMY89uC9j9CBXC
# expected: { "ismine": true, "isvalid": true, ... }
```

If any check fails, **stop here**. Do not proceed to broadcast.

## First-light

### Step 1 — Extract the Conclave Key #1 WIF

```bash
WIF=$(Offerings-cli dumpprivkey Qcad56y76jCGgjkhGHPyCMY89uC9j9CBXC)
```

`$WIF` is now in the shell's environment — do not log it, do not echo it,
do not paste it into a chat client. Treat as a long-lived secret.

### Step 2 — Restart the daemon with `-checkpointkey`

```bash
# Stop the current daemon cleanly
Offerings-cli stop
sleep 5  # wait for shutdown : done

# Restart with the broadcaster privkey. NEVER place -checkpointkey=$WIF in
# Offerings.conf (it's globally readable). Use the command line so the WIF
# only lives in the kernel's argv table.
#
# -checkpointdepth is REQUIRED for steady-state operation: the tip-advance
# auto-broadcast (main.cpp ProcessBlock) is gated on checkpointdepth >= 0,
# and its default is -1 (disabled). Without it the daemon signs manual
# sendcheckpoint calls but never auto-broadcasts, and the sync checkpoint
# silently goes stale as the chain advances. 100 blocks matches the
# MAX_REORG_DEPTH rationale in Step 4.
Offeringsd -checkpointkey="$WIF" -checkpointdepth=100 -daemon
unset WIF  # immediately scrub from the shell env
```

For a systemd-managed deployment, prefer an override file with
`EnvironmentFile=` pointing at a `0600`-mode file containing `WIF=…` and
`ExecStart=Offeringsd -checkpointkey=$WIF -daemon`. The systemd cgroup
inherits a tighter audit posture than an interactive shell.

### Step 3 — Verify the daemon accepted the privkey

```bash
# Daemon log should show no "SetCheckpointPrivKey" error at startup.
# Failure modes:
#   - "Checkpoint master key invalid"   ⇒ WIF parse error (wrong network byte)
#   - "Unable to sign checkpoint"       ⇒ corrupted privkey
tail -50 ~/.Offering/debug.log | grep -iE "checkpoint|init message"
```

If the daemon silently accepted the privkey, `Offerings-cli getinfo`
should now return without error and `getblockcount` should be unchanged.

### Step 4 — Issue the first signed checkpoint

Pick a target height that's recent but past plausible reorg depth. For a
2018-vintage chain at 60s blocks, **100 blocks back from tip** is a sensible
starting point — 100 minutes deep, well past `MAX_REORG_DEPTH=100` near-tip
contention, and recent enough that the checkpoint actually constrains
something.

```bash
TIP=$(Offerings-cli getblockcount)
TARGET_H=$((TIP - 100))
TARGET_HASH=$(Offerings-cli getblockhash $TARGET_H)
echo "broadcasting checkpoint at h=$TARGET_H hash=$TARGET_HASH"

Offerings-cli sendcheckpoint "$TARGET_HASH"
```

Expected response (JSON):

```json
{
    "synccheckpoint" : "<TARGET_HASH>",
    "height" : <TARGET_H>,
    "timestamp" : <unix-time>,
    "subscribemode" : "enforce",
    "checkpointmaster" : true
}
```

`"checkpointmaster" : true` confirms this daemon is now the broadcaster.

### Step 5 — Verify propagation

On a separate Phase-2-aware node (any peer that is not the broadcaster),
within a few seconds:

```bash
Offerings-cli getinfo | grep -i checkpoint
# expected: synccheckpoint hash matches what was broadcast
tail -20 ~/.Offering/debug.log | grep -i ProcessSyncCheckpoint
# expected: "ProcessSyncCheckpoint: sync-checkpoint at <hash>"
```

Absence of the log line within 60 seconds ⇒ propagation failed; check
peer connections, daemon versions, and the broadcaster's debug.log for
`SendSyncCheckpoint` and `RelayTo` lines.

## Steady-state operation

After first-light, the daemon takes over:

- **Tip-advance auto-broadcast.** `src/main.cpp` ProcessBlock re-broadcasts
  a fresh checkpoint at tip minus `-checkpointdepth` each time a block is
  accepted — peer-received **or locally produced** (pool `submitblock`,
  internal miner; fixed in #50 — pre-fix binaries only fired on
  peer-origin blocks, so a block-producing broadcaster went stale). No
  cron required — **but only when `-checkpointdepth >= 0` was supplied**
  (default -1 disables the auto-path entirely; verified during the v2.1.0
  testnet rehearsal, where a broadcaster running with only
  `-checkpointkey` signed the manual first-light checkpoint and then went
  silent for 590 blocks). The auto-path is suppressed during initial
  block download.
- **Restart behavior.** The current sync checkpoint is persisted and
  reloaded at startup (#50 — pre-fix binaries zeroed the in-memory view
  on every restart, master and recipient alike, until the next broadcast
  arrived). Newly connected peers also receive the current checkpoint
  message at version handshake (#50), so a restarted recipient reacquires
  it immediately.
- **`-checkpointkey` persistence.** The privkey lives only in the running
  process's memory and the systemd EnvironmentFile (if used). A daemon
  restart drops it; the operator must re-supply `-checkpointkey` (and
  `-checkpointdepth`) on every restart for the broadcaster role to resume.
- **Monitoring.** Watch for these log lines:
  - `SendSyncCheckpoint: hashCheckpoint=…` — broadcaster acting (expected, frequent)
  - `ProcessSyncCheckpoint: sync-checkpoint at …` — recipient accepted (expected)
  - `AcceptBlock() : rejected by synchronized checkpoint` — a peer tried to
    push a conflicting block. **Frequency matters**: occasional ⇒ benign
    reorg loser; sustained ⇒ active attack attempt. Investigate.
  - `CSyncCheckpoint::CheckSignature() : verify signature failed` — someone
    is pushing a forged checkpoint message. Should never appear on the
    broadcaster; on recipients it indicates a misbuilt peer or hostile
    traffic.

## Recipient-side opt-out / advisory mode

By default recipients enforce checkpoints. To run a recipient in
advisory-only mode (log but don't reject):

```bash
# Persistent (Offerings.conf): checkpointenforce=0
# Runtime:
Offerings-cli enforcecheckpoint false
```

Used by a node operator who wants to observe ACP behavior without binding
their consensus to it. **Not recommended for production** — it negates
the 51% defense.

## Failure / rollback

### "I want to stop being broadcaster"

Restart the daemon without `-checkpointkey`. `strMasterPrivKey` is process-
local; new daemon won't broadcast.

### "The privkey may be compromised"

Coordinated chainparams rotation required. Separate internal procedure;
not in scope for this public runbook.

### "My daemon crashed mid-broadcast"

`CSyncCheckpoint` state is held in-memory plus persisted via
`pblocktree->WriteCheckpointPubKey` / `WriteSyncCheckpoint`. On restart,
the daemon will re-validate the persisted state. No special recovery —
just restart with `-checkpointkey` as before.

## Open subquestions (track separately from this runbook)

1. ~~**Cadence heartbeat**~~ — resolved by #50: the version-handshake
   relay delivers the current checkpoint to any peer that joined after
   the most recent gossip wave, and the auto-broadcast now also fires on
   locally-produced blocks. An operator-side heartbeat cron remains a
   harmless belt-and-braces during rollout of mixed pre/post-#50
   binaries; it can be retired once the broadcaster runs a #50 build.
2. **Depth tuning** — `MAX_REORG_DEPTH=100` ≈ 100 minutes at 60s blocks.
   Worth tuning if real-world reorg statistics suggest a different bound.

Operational items deferred to internal documentation are not enumerated
here. The Conclave maintains a separate brief.

## Cross-references

- `contrib/phase2-acp/AUDIT.md` — what was already in the tree (P2P
  handler, RPCs, init wiring) before #40 woke the ACP up.
- `qa/rpc-tests/phase2_acp.py` — regtest harness verifying broadcaster
  signs, peer accepts, conflicting chain rejected.
- `src/checkpoints.{cpp,h}` — implementation.
- `src/chainparams.cpp:188` — `vConclaveKeys[0]` definition.
- `src/init.cpp:574-577` — `-checkpointkey` wiring.
- `src/main.cpp:3033` — tip-advance auto-broadcast trigger.
- `src/main.cpp:2825-2833` — `AcceptBlock` checkpoint-enforcement hook.

## Provenance

Drafted 2026-06-17 during issue #40 first-light. Reviewed by [TBD] before
operator action.
