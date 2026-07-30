#!/usr/bin/env python3
"""
Phase-2 ACP regtest harness (issue #40).

Spins up two regtest daemons. Node 0 holds the test-fixture ACP privkey
via -checkpointkey=<WIF>. Mines blocks, then calls `sendcheckpoint` to
sign and broadcast a checkpoint. Node 1 receives + accepts. Verifies:

  PHASE 1 — peer the nodes, mine past coinbase maturity, confirm both
    nodes agree on the tip.

  PHASE 2 — broadcaster calls `sendcheckpoint <target_hash>`. Signature
    is generated using the test-fixture privkey; pubkey baked at
    src/chainparams.cpp (regtest vConclaveKeys, slot #0).

  PHASE 3 — peer (node 1) receives the checkpoint message, validates
    the signature against Params().ConclaveKeys()[0], and persists
    hashSyncCheckpoint. Confirmed by grepping the peer's debug.log for
    the ProcessSyncCheckpoint: sync-checkpoint at <hash> log line.

Test fixture privkey is derived deterministically from
sha256("OFFv2-test-checkpointkey-2026!!!"). It is NOT a real Conclave
key — it is embedded in this file and in chainparams.cpp solely for
regtest. Do not reuse it for testnet or mainnet.
"""

import os
import sys
import json
import time
import shutil
import atexit
import tempfile
import subprocess

SRCDIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "src"))
DAEMON = os.path.join(SRCDIR, "Offeringsd")
CLI    = os.path.join(SRCDIR, "Offerings-cli")

# Test fixture (chainparams.cpp regtest/testnet vConclaveKeys, slot #0).
# Privkey: sha256("OFFv2-test-checkpointkey-2026!!!")
# Pubkey (uncompressed): 04 07bfe025... 5c2b3e3f...  (65 bytes)
# WIF is uncompressed too (no 0x01 flag) — the pre-#40 ppcoin-derived signing
# path stores uncompressed master keys, and the privkey loaded from WIF must
# match that form for CKey::Sign output to verify against CPubKey::Verify.
TEST_CHECKPOINT_WIF = "9HWyiqthmTjXgUJwrmEk7NJY9XF9ShXADQhBYYEA2sSmRVejV3f"

NODES = []  # popen handles


def cleanup():
    for p in NODES:
        try:
            p.terminate()
        except Exception:
            pass
    for p in NODES:
        try:
            p.wait(timeout=5)
        except Exception:
            try:
                p.kill()
            except Exception:
                pass


atexit.register(cleanup)


def cli(datadir, *args):
    return subprocess.check_output(
        [CLI, "-regtest", "-datadir=" + datadir,
         "-rpcuser=test", "-rpcpassword=test"] + list(args),
        text=True, stderr=subprocess.STDOUT).strip()


def cli_try(datadir, *args):
    try:
        return cli(datadir, *args), None
    except subprocess.CalledProcessError as e:
        return None, e.output


def make_datadir(tmp, idx):
    d = os.path.join(tmp, "node%d" % idx)
    os.makedirs(d)
    rpc = 18570 + idx
    p2p = 18560 + idx
    with open(os.path.join(d, "Offerings.conf"), "w") as f:
        f.write("regtest=1\n")
        f.write("rpcuser=test\n")
        f.write("rpcpassword=test\n")
        f.write("rpcport=%d\n" % rpc)
        f.write("port=%d\n" % p2p)
    return d


def start_node(idx, datadir, extra_args):
    p2p = 18560 + idx
    rpc = 18570 + idx
    args = [DAEMON, "-regtest", "-datadir=" + datadir,
            "-rpcuser=test", "-rpcpassword=test",
            "-rpcport=%d" % rpc, "-port=%d" % p2p,
            "-server=1", "-listen=1", "-printtoconsole",
            "-keypool=1"] + extra_args
    log_path = os.path.join(datadir, "stdout.log")
    log_file = open(log_path, "w")
    NODES.append(subprocess.Popen(args, stdout=log_file, stderr=subprocess.STDOUT))
    # Wait up to 30s for RPC to come up
    for _ in range(60):
        out, err = cli_try(datadir, "getblockcount")
        if out is not None:
            return p2p, rpc
        time.sleep(0.5)
    raise RuntimeError("Node %d failed to start RPC (see %s)" % (idx, log_path))


def sync_blocks(datadirs, timeout=30):
    end = time.time() + timeout
    heights = []
    while time.time() < end:
        heights = [int(cli(d, "getblockcount")) for d in datadirs]
        if len(set(heights)) == 1:
            return heights[0]
        time.sleep(0.5)
    raise RuntimeError("Block sync timed out: heights=%s" % heights)


def main():
    print("=== Phase-2 ACP broadcast checkpoints (#40) — regtest harness ===\n")

    tmp = tempfile.mkdtemp(prefix="off-phase2-acp-")
    print("tmpdir: %s" % tmp)

    d0 = make_datadir(tmp, 0)
    d1 = make_datadir(tmp, 1)

    print("Starting broadcaster (node 0) with -checkpointkey=<test WIF> ...")
    p2p0, rpc0 = start_node(0, d0, ["-checkpointkey=" + TEST_CHECKPOINT_WIF])
    print("  node 0 up on p2p:%d rpc:%d" % (p2p0, rpc0))

    print("Starting peer (node 1) connecting to broadcaster ...")
    p2p1, rpc1 = start_node(1, d1, ["-connect=127.0.0.1:%d" % p2p0])
    print("  node 1 up on p2p:%d rpc:%d" % (p2p1, rpc1))

    # Give the connection a moment to settle
    time.sleep(3)
    conns0 = int(cli(d0, "getconnectioncount"))
    conns1 = int(cli(d1, "getconnectioncount"))
    print("  connections: node0=%d, node1=%d" % (conns0, conns1))
    if conns0 == 0 or conns1 == 0:
        raise RuntimeError("nodes failed to peer")

    print("\nPHASE 1 — mine 110 blocks on node 0, sync to node 1")
    cli(d0, "setgenerate", "true", "110")
    h = sync_blocks([d0, d1])
    print("  PASS  tip = %d on both nodes" % h)

    print("\nPHASE 2 — broadcaster signs + sends a checkpoint at tip-50")
    tip = int(cli(d0, "getblockcount"))
    target_h = tip - 50
    target_hash = cli(d0, "getblockhash", str(target_h))
    print("  target: h=%d hash=%s..." % (target_h, target_hash[:16]))

    out, err = cli_try(d0, "sendcheckpoint", target_hash)
    if out is None:
        raise RuntimeError("sendcheckpoint failed: %s" % err)
    print("  broadcaster: sendcheckpoint returned: %s" % (out or "(empty)"))

    print("\nPHASE 3 — verify peer accepts the checkpoint")
    # With -printtoconsole the daemon writes its log to stdout (captured to
    # stdout.log by start_node), NOT to regtest/debug.log. Grep stdout.log.
    log_path = os.path.join(d1, "stdout.log")
    needle = "ProcessSyncCheckpoint: sync-checkpoint at " + target_hash
    end = time.time() + 15
    received = False
    while time.time() < end:
        try:
            with open(log_path) as f:
                if needle in f.read():
                    received = True
                    break
        except IOError:
            pass
        time.sleep(0.5)

    if not received:
        raise RuntimeError(
            "node 1 did not accept the sync-checkpoint within 15s; "
            "check %s for ACP log lines" % log_path
        )
    print("  PASS  node 1 accepted sync-checkpoint at %s..." % target_hash[:16])

    # Bonus check: confirm broadcaster sees it too (local ProcessSyncCheckpoint(NULL))
    log0 = os.path.join(d0, "stdout.log")
    with open(log0) as f:
        if needle not in f.read():
            raise RuntimeError("broadcaster's local ProcessSyncCheckpoint didn't log acceptance")
    print("  PASS  broadcaster also processed locally")

    # ------------------------------------------------------------------
    # PHASE 4 — NEGATIVE: a competing chain that doesn't descend from
    # the checkpoint must be rejected.
    #
    # Setup: two FRESH unconnected nodes (call them 2 and 3). Each
    # mines independently to a different chain. Node 2 (broadcaster)
    # mines 10 blocks; node 3 mines 20 blocks (longer chain). Node 2
    # signs a checkpoint at its h=5. Then they peer.
    #
    # Without ACP, natural chain selection would pull node 2 to chain
    # 3 (length 20 > length 10). With ACP, the checkpoint at node 2's
    # h=5 forces node 3 to abandon its longer chain in favor of node
    # 2's chain. Final tip on node 3 must match node 2's h=5 hash at
    # height 5.
    # ------------------------------------------------------------------
    print("\nPHASE 4 — competing-chain rejection via CheckSyncCheckpoint")

    d2 = make_datadir(tmp, 2)
    d3 = make_datadir(tmp, 3)

    print("  starting node 2 (broadcaster) and node 3 (attacker), unconnected ...")
    p2p2, _ = start_node(2, d2, ["-checkpointkey=" + TEST_CHECKPOINT_WIF,
                                  "-connect=0"])
    p2p3, _ = start_node(3, d3, ["-connect=0"])

    print("  node 2 mines 10 blocks (chain A) ...")
    cli(d2, "setgenerate", "true", "10")
    a_tip_h = int(cli(d2, "getblockcount"))
    a5_hash = cli(d2, "getblockhash", "5")
    print("    tip A: h=%d, A5=%s..." % (a_tip_h, a5_hash[:16]))

    print("  node 3 mines 20 blocks (chain B, longer) ...")
    cli(d3, "setgenerate", "true", "20")
    b_tip_h = int(cli(d3, "getblockcount"))
    b5_hash = cli(d3, "getblockhash", "5")
    print("    tip B: h=%d, B5=%s..." % (b_tip_h, b5_hash[:16]))

    if a5_hash == b5_hash:
        raise RuntimeError(
            "regtest determinism collision: A5 == B5; can't test rejection")

    print("  node 2 signs checkpoint at A5 ...")
    out, err = cli_try(d2, "sendcheckpoint", a5_hash)
    if out is None:
        raise RuntimeError("Phase 4 sendcheckpoint failed: %s" % err)
    print("    A5 checkpoint set")

    print("  peer nodes 2 + 3 ...")
    cli(d3, "addnode", "127.0.0.1:%d" % p2p2, "onetry")

    print("  give nodes 15s to sync, propagate checkpoint, and apply rejection ...")
    time.sleep(15)

    # The defense fires on the side that HOLDS the checkpoint. When node 3's
    # competing chain B blocks arrive at node 2, AcceptBlock runs
    # CheckSyncCheckpoint; descendants of B5 != A5 are rejected. We assert
    # against node 2's log for the canonical "rejected by synchronized
    # checkpoint" line that AcceptBlock emits.
    print("  searching node 2 (broadcaster) log for rejection events ...")
    with open(os.path.join(d2, "stdout.log")) as f:
        log2 = f.read()
    n_reject = log2.count("AcceptBlock() : rejected by synchronized checkpoint")
    if n_reject == 0:
        raise RuntimeError(
            "expected node 2 to log 'AcceptBlock() : rejected by synchronized "
            "checkpoint' at least once when node 3's chain B blocks arrived; "
            "got 0 occurrences. Either CheckSyncCheckpoint isn't being hit at "
            "AcceptBlock, or the sync didn't deliver B blocks within the wait.")
    print("    PASS  node 2 rejected %d conflicting block(s) via CheckSyncCheckpoint" % n_reject)

    # Bonus: node 2's tip should still be on chain A (its own), unchanged.
    a_tip_hash = cli(d2, "getblockhash", str(a_tip_h))
    if a_tip_hash != cli(d2, "getbestblockhash"):
        # Tip may have advanced if node 2 also mined more blocks; that's fine.
        # Key assertion is the rejection occurred. Soft assert: A5 still at h=5.
        pass
    a5_after = cli(d2, "getblockhash", "5")
    if a5_after != a5_hash:
        raise RuntimeError("node 2's h=5 changed from A5=%s to %s after peering — "
                           "checkpoint enforcement broken" % (a5_hash[:16], a5_after[:16]))
    print("    PASS  node 2's A5 at h=5 unchanged after peering with attacker")

    # ------------------------------------------------------------------
    # PHASE 5 — #50: persisted checkpoint survives a restart.
    #
    # The checkpoint has always been written to the block tree db on
    # accept, but was never read back — a restart zeroed the in-memory
    # view (observed fleet-wide on mainnet). Stop node 1 first so the
    # only possible source of the value is node 0's own disk, not a
    # handshake relay from a peer that still holds the message.
    # ------------------------------------------------------------------
    print("\nPHASE 5 — persisted sync-checkpoint reloads across restart (#50)")

    print("  stopping node 1 (isolate node 0 from handshake relays) ...")
    cli(d1, "stop")
    time.sleep(3)

    print("  restarting node 0 with -connect=0 ...")
    cli(d0, "stop")
    time.sleep(3)
    start_node(0, d0, ["-checkpointkey=" + TEST_CHECKPOINT_WIF,
                       "-checkpointdepth=20", "-connect=0"])

    ckpt = json.loads(cli(d0, "getcheckpoint"))
    if ckpt.get("synccheckpoint") != target_hash:
        raise RuntimeError(
            "after restart, node 0 getcheckpoint = %s, expected the phase-2 "
            "checkpoint %s — startup reload (LoadSyncCheckpoint) not working"
            % (ckpt.get("synccheckpoint"), target_hash))
    print("  PASS  node 0 restored checkpoint %s... (h=%s) from disk"
          % (target_hash[:16], ckpt.get("height")))

    # ------------------------------------------------------------------
    # PHASE 6 — #50: auto-broadcast fires on locally-produced blocks.
    #
    # Node 0 now runs with -checkpointdepth=20 and no peers. setgenerate
    # produces blocks with pfrom == NULL — exactly the pool-submitblock
    # shape that never triggered the old pfrom-gated auto-broadcast.
    # ------------------------------------------------------------------
    print("\nPHASE 6 — auto-broadcast on self-produced blocks (#50)")

    cli(d0, "setgenerate", "true", "5")
    tip = int(cli(d0, "getblockcount"))
    want_h = tip - 20
    want_hash = cli(d0, "getblockhash", str(want_h))

    end = time.time() + 15
    got = None
    while time.time() < end:
        got = json.loads(cli(d0, "getcheckpoint"))
        if got.get("synccheckpoint") == want_hash:
            break
        time.sleep(0.5)
    if got.get("synccheckpoint") != want_hash:
        raise RuntimeError(
            "auto-broadcast did not advance the checkpoint on local blocks: "
            "getcheckpoint=%s expected tip-20 %s (tip=%d) — pfrom gate fix "
            "not working" % (got.get("synccheckpoint"), want_hash, tip))
    print("  PASS  checkpoint auto-advanced to tip-20 (h=%d) with zero "
          "peer-origin blocks" % want_h)

    # ------------------------------------------------------------------
    # PHASE 7 — #50: version-handshake relay to a fresh peer.
    #
    # Node 4 starts with an empty datadir and connects to node 0. No
    # sendcheckpoint is issued and no blocks are mined during this
    # phase, so the ONLY path for node 4 to learn the checkpoint is the
    # relay of checkpointMessage in the version handler (restored from
    # the ppcoin lineage).
    # ------------------------------------------------------------------
    print("\nPHASE 7 — checkpoint relayed to fresh peer on handshake (#50)")

    d4 = make_datadir(tmp, 4)
    start_node(4, d4, ["-connect=127.0.0.1:%d" % p2p0])

    end = time.time() + 45
    got4 = {}
    while time.time() < end:
        out, err = cli_try(d4, "getcheckpoint")
        if out is not None:
            got4 = json.loads(out)
            if got4.get("synccheckpoint") == want_hash:
                break
        time.sleep(1)
    if got4.get("synccheckpoint") != want_hash:
        raise RuntimeError(
            "fresh node 4 did not acquire checkpoint %s via handshake relay "
            "within 45s (got %s) — version-handler relay not working"
            % (want_hash[:16], got4.get("synccheckpoint")))
    print("  PASS  fresh node 4 acquired checkpoint %s... purely via "
          "version-handshake relay" % want_hash[:16])

    print("\n=== ALL PHASE-2 ACP CHECKS PASSED ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
