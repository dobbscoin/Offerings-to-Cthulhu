// Copyright (c) 2026 The Offerings Conclave
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_H
#define BITCOIN_STRATUM_H

#include "sync.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace boost { class thread; }

/** One mining.notify job as received from the pool (hex fields kept in
 *  wire format; see research/stratum-dialect-probe-2026-08-07.md for the
 *  decoded layout, byte-order conventions, and coinbase template shape). */
struct StratumJob
{
    std::string strJobId;
    std::string strPrevHash;   // dword-order-reversed RPC hash, as sent
    std::string strCoinb1;
    std::string strCoinb2;
    std::vector<std::string> vMerkleBranch;
    std::string strVersion;    // big-endian hex int32
    std::string strNBits;      // compact target, big-endian hex
    std::string strNTime;      // big-endian hex epoch
    bool fCleanJobs;

    StratumJob() : fCleanJobs(false) {}
    bool IsNull() const { return strJobId.empty(); }
};

/** Stratum v1 client.
 *  Phase 1: protocol layer (subscribe, authorize, job + difficulty tracking,
 *  reconnect with backoff).
 *  Phase 2: hash workers — assemble the 80-byte header from the current job
 *  (coinbase splice, merkle fold, dword-order prevhash transform per
 *  research/stratum-dialect-probe-2026-08-07.md), grind Quark Hash9 over the
 *  nonce, submit shares that meet the vardiff target, track accept/reject.
 *
 *  Runs its own threads; all getters are thread-safe snapshots. */
class CStratumClient
{
public:
    CStratumClient();
    ~CStratumClient();

    //! Spawn the client thread and nThreadsIn hash workers (0 = protocol only).
    bool Start(const std::string& strHostIn, int nPortIn, const std::string& strUserIn,
               int nThreadsIn = 0);
    //! Signal shutdown and join the thread. Safe to call when not running.
    void Stop();

    bool IsRunning() const;
    bool IsConnected() const;
    bool IsAuthorized() const;
    double GetDifficulty() const;
    std::string GetExtraNonce1() const;
    int GetExtraNonce2Size() const;
    //! Copy out the latest job; returns false if none received yet.
    bool GetCurrentJob(StratumJob& jobOut) const;
    int64_t GetJobsReceived() const;
    std::string GetLastError() const;
    int64_t GetSharesSubmitted() const;
    int64_t GetSharesAccepted() const;
    int64_t GetSharesRejected() const;
    //! Hashes per second across all workers (decaying snapshot).
    double GetHashRate() const;

private:
    void ThreadStratum();
    //! One connect→subscribe→authorize→read-loop session. Returns on error
    //! or shutdown; caller decides whether to reconnect.
    void RunSession();
    void HandleLine(const std::string& strLine);
    void ThreadWorker(int nWorkerId);
    //! Serialize a submit line onto the live session socket (thread-safe).
    //! Returns false if not connected.
    bool SubmitShare(const std::string& strJobId, const std::string& strExtraNonce2,
                     const std::string& strNTime, const std::string& strNonce);

    // no copying — owns a thread and a live socket
    CStratumClient(const CStratumClient&);
    CStratumClient& operator=(const CStratumClient&);

    mutable CCriticalSection cs;
    std::string strHost;
    int nPort;
    std::string strUser;

    bool fConnected;
    bool fAuthorized;
    double dDifficulty;
    std::string strExtraNonce1;
    int nExtraNonce2Size;
    StratumJob currentJob;
    int64_t nJobsReceived;
    std::string strLastError;

    int nThreads;
    int64_t nSharesSubmitted;
    int64_t nSharesAccepted;
    int64_t nSharesRejected;
    int64_t nHashCounter;          // running total, sampled for the rate
    mutable int64_t nHashRateTime; // last sample time (ms)
    mutable int64_t nHashRateCount;// counter value at last sample
    mutable double dHashRate;

    //! Write side of the live session socket; guarded by cs_write. Owned by
    //! RunSession — NULL whenever no session is up.
    void* pSocket;
    CCriticalSection cs_write;
    int nSubmitId;             // next request id for mining.submit (>= 10)

    boost::thread* pthreadClient;
    std::vector<boost::thread*> vWorkers;
    volatile bool fShutdown;
    //! Raw fd of the live socket so Stop() can unblock a blocking read.
    volatile int nSocketFd;
};

/** Global client instance driven by -stratum=<host:port> / -stratumuser=<Q-address>
 *  (hidden debug args; see init.cpp). NULL when not enabled. */
extern CStratumClient* g_pStratumClient;

//! Called from init: start the global client if -stratum is configured.
bool StartStratumIfConfigured();
//! Called from shutdown: stop and delete the global client if running.
void StopStratum();

#endif // BITCOIN_STRATUM_H
