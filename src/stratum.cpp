// Copyright (c) 2013-2014 The Offerings developers
// Copyright (c) 2026 The Offerings Conclave / SubGenius.Finance community
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stratum.h"

#include "core.h"
#include "hash.h"
#include "uint256.h"
#include "util.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include <cmath>
#include <cstring>

using namespace json_spirit;
using namespace std;

CStratumClient* g_pStratumClient = NULL;
CCriticalSection cs_stratum_control;

CStratumClient::CStratumClient()
    : nPort(0),
      fConnected(false),
      fAuthorized(false),
      dDifficulty(0.0),
      nExtraNonce2Size(0),
      nJobsReceived(0),
      nThreads(0),
      nSharesSubmitted(0),
      nSharesAccepted(0),
      nSharesRejected(0),
      nHashCounter(0),
      nHashRateTime(0),
      nHashRateCount(0),
      dHashRate(0.0),
      pSocket(NULL),
      nSubmitId(10),
      pthreadClient(NULL),
      fShutdown(false),
      nSocketFd(-1)
{
}

CStratumClient::~CStratumClient()
{
    Stop();
}

bool CStratumClient::Start(const std::string& strHostIn, int nPortIn, const std::string& strUserIn,
                           int nThreadsIn)
{
    if (pthreadClient)
        return false;
    strHost = strHostIn;
    nPort = nPortIn;
    strUser = strUserIn;
    nThreads = nThreadsIn;
    fShutdown = false;
    pthreadClient = new boost::thread(boost::bind(&CStratumClient::ThreadStratum, this));
    for (int i = 0; i < nThreads; i++)
        vWorkers.push_back(new boost::thread(boost::bind(&CStratumClient::ThreadWorker, this, i)));
    return true;
}

void CStratumClient::Stop()
{
    fShutdown = true;
    // Unblock any blocking read by shutting down (not closing) the socket
    // under it — asio's destructor still owns the close. Can't use the
    // closesocket macro here: compat.h maps it to myclosesocket(SOCKET&).
    intptr_t fd = nSocketFd;
    if (fd != -1) {
#ifdef WIN32
        ::shutdown((SOCKET)fd, 2 /* SD_BOTH */);
#else
        ::shutdown((int)fd, 2 /* SHUT_RDWR */);
#endif
    }
    if (pthreadClient) {
        pthreadClient->join();
        delete pthreadClient;
        pthreadClient = NULL;
    }
    for (unsigned int i = 0; i < vWorkers.size(); i++) {
        vWorkers[i]->join();
        delete vWorkers[i];
    }
    vWorkers.clear();
}

bool CStratumClient::IsRunning() const { return pthreadClient != NULL; }

bool CStratumClient::IsConnected() const
{
    LOCK(cs);
    return fConnected;
}

std::string CStratumClient::GetHost() const
{
    LOCK(cs);
    return strHost;
}

int CStratumClient::GetPort() const
{
    LOCK(cs);
    return nPort;
}

std::string CStratumClient::GetUser() const
{
    LOCK(cs);
    return strUser;
}

int CStratumClient::GetThreads() const
{
    LOCK(cs);
    return nThreads;
}

bool CStratumClient::IsAuthorized() const
{
    LOCK(cs);
    return fAuthorized;
}

double CStratumClient::GetDifficulty() const
{
    LOCK(cs);
    return dDifficulty;
}

std::string CStratumClient::GetExtraNonce1() const
{
    LOCK(cs);
    return strExtraNonce1;
}

int CStratumClient::GetExtraNonce2Size() const
{
    LOCK(cs);
    return nExtraNonce2Size;
}

bool CStratumClient::GetCurrentJob(StratumJob& jobOut) const
{
    LOCK(cs);
    if (currentJob.IsNull())
        return false;
    jobOut = currentJob;
    return true;
}

int64_t CStratumClient::GetJobsReceived() const
{
    LOCK(cs);
    return nJobsReceived;
}

std::string CStratumClient::GetLastError() const
{
    LOCK(cs);
    return strLastError;
}

void CStratumClient::ThreadStratum()
{
    RenameThread("offerings-stratum");
    LogPrintf("stratum: client thread started (%s:%d, user %s)\n", strHost, nPort, strUser);

    int64_t nBackoff = 5;
    while (!fShutdown) {
        RunSession();
        if (fShutdown)
            break;
        LogPrintf("stratum: disconnected, reconnecting in %ds\n", (int)nBackoff);
        for (int64_t i = 0; i < nBackoff * 10 && !fShutdown; i++)
            MilliSleep(100);
        nBackoff = std::min<int64_t>(nBackoff * 2, 60); // 5,10,20,40,60,60,...
        {
            LOCK(cs);
            if (fConnected)
                nBackoff = 5; // last session got somewhere; restart the ladder
        }
    }
    LogPrintf("stratum: client thread exiting\n");
}

void CStratumClient::RunSession()
{
    {
        LOCK(cs);
        fConnected = false;
        fAuthorized = false;
    }

    try {
        boost::asio::io_service io_service;
        boost::asio::ip::tcp::resolver resolver(io_service);
        boost::asio::ip::tcp::resolver::query query(strHost, strprintf("%d", nPort));
        boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        boost::asio::ip::tcp::socket socket(io_service);
        boost::asio::connect(socket, endpoint_iterator);
        nSocketFd = (intptr_t)socket.native_handle();
        {
            LOCK(cs_write);
            pSocket = &socket;
        }

        {
            LOCK(cs);
            fConnected = true;
            strLastError.clear();
        }
        LogPrintf("stratum: connected to %s:%d\n", strHost, nPort);

        // subscribe (id 1) then authorize (id 2); responses matched by id below
        std::string strSubscribe =
            "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"" +
            FormatFullVersion() + "\"]}\n";
        boost::asio::write(socket, boost::asio::buffer(strSubscribe));
        std::string strAuthorize =
            "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"" +
            strUser + "\",\"x\"]}\n";
        boost::asio::write(socket, boost::asio::buffer(strAuthorize));

        boost::asio::streambuf response;
        std::string strBuffer;
        while (!fShutdown) {
            boost::asio::read_until(socket, response, "\n");
            // Drain everything received, but only hand off complete lines —
            // a partial line after the last '\n' stays buffered for the next read.
            strBuffer.append(std::istreambuf_iterator<char>(&response),
                             std::istreambuf_iterator<char>());
            size_t pos;
            while ((pos = strBuffer.find('\n')) != std::string::npos) {
                std::string strLine = strBuffer.substr(0, pos);
                strBuffer.erase(0, pos + 1);
                if (!strLine.empty() && strLine[strLine.size() - 1] == '\r')
                    strLine.erase(strLine.size() - 1);
                if (!strLine.empty())
                    HandleLine(strLine);
            }
        }
        {
            LOCK(cs_write);
            pSocket = NULL;
        }
        nSocketFd = -1;
    } catch (const std::exception& e) {
        {
            LOCK(cs_write);
            pSocket = NULL;
        }
        nSocketFd = -1;
        {
            LOCK(cs);
            strLastError = e.what();
            fConnected = false;
            fAuthorized = false;
        }
        if (!fShutdown)
            LogPrintf("stratum: session error: %s\n", e.what());
    }
}

void CStratumClient::HandleLine(const std::string& strLine)
{
    LogPrint("stratum", "stratum: <<< %s\n", strLine);

    Value valLine;
    if (!read_string(strLine, valLine) || valLine.type() != obj_type) {
        LogPrintf("stratum: unparseable line from pool (%u bytes)\n", (unsigned)strLine.size());
        return;
    }
    const Object& obj = valLine.get_obj();

    Value valMethod = find_value(obj, "method");
    if (valMethod.type() == str_type) {
        const std::string strMethod = valMethod.get_str();
        Value valParams = find_value(obj, "params");
        if (valParams.type() != array_type)
            return;
        const Array& params = valParams.get_array();

        if (strMethod == "mining.set_difficulty") {
            if (params.size() >= 1 && (params[0].type() == real_type || params[0].type() == int_type)) {
                double dNew = (params[0].type() == real_type) ? params[0].get_real()
                                                             : (double)params[0].get_int64();
                LOCK(cs);
                dDifficulty = dNew;
                LogPrintf("stratum: difficulty set to %g\n", dNew);
            }
        } else if (strMethod == "mining.notify") {
            // [jobId, prevHash, coinb1, coinb2, merkleBranch[], version, nBits, nTime, clean]
            if (params.size() < 9) {
                LogPrintf("stratum: short mining.notify (%u params)\n", (unsigned)params.size());
                return;
            }
            StratumJob job;
            job.strJobId = params[0].get_str();
            job.strPrevHash = params[1].get_str();
            job.strCoinb1 = params[2].get_str();
            job.strCoinb2 = params[3].get_str();
            const Array& branch = params[4].get_array();
            for (unsigned int i = 0; i < branch.size(); i++)
                job.vMerkleBranch.push_back(branch[i].get_str());
            job.strVersion = params[5].get_str();
            job.strNBits = params[6].get_str();
            job.strNTime = params[7].get_str();
            job.fCleanJobs = params[8].get_bool();

            LOCK(cs);
            currentJob = job;
            nJobsReceived++;
            LogPrintf("stratum: job %s (prev %s.., nbits %s, ntime %s%s, %u branch)\n",
                      job.strJobId, job.strPrevHash.substr(0, 16), job.strNBits,
                      job.strNTime, job.fCleanJobs ? ", CLEAN" : "",
                      (unsigned)job.vMerkleBranch.size());
        } else {
            LogPrint("stratum", "stratum: ignoring method %s\n", strMethod);
        }
        return;
    }

    // A response to one of our requests: match by id.
    Value valId = find_value(obj, "id");
    Value valResult = find_value(obj, "result");
    Value valError = find_value(obj, "error");
    int64_t nId = (valId.type() == int_type) ? valId.get_int64() : -1;

    if (valError.type() != null_type) {
        std::string strErr = write_string(valError, false);
        LOCK(cs);
        strLastError = strErr;
        LogPrintf("stratum: request id %d rejected: %s\n", (int)nId, strErr);
        return;
    }

    if (nId == 1 && valResult.type() == array_type) {
        // subscribe result: [[subscriptions..], extranonce1, extranonce2_size]
        const Array& res = valResult.get_array();
        if (res.size() >= 3 && res[1].type() == str_type && res[2].type() == int_type) {
            LOCK(cs);
            strExtraNonce1 = res[1].get_str();
            nExtraNonce2Size = res[2].get_int();
            LogPrintf("stratum: subscribed, extranonce1=%s extranonce2_size=%d\n",
                      strExtraNonce1, nExtraNonce2Size);
        } else {
            LogPrintf("stratum: malformed subscribe result\n");
        }
    } else if (nId == 2) {
        bool fOk = (valResult.type() == bool_type) && valResult.get_bool();
        {
            LOCK(cs);
            fAuthorized = fOk;
        }
        LogPrintf("stratum: authorize %s for %s\n", fOk ? "ACCEPTED" : "REJECTED", strUser);
    } else if (nId >= 10) {
        // response to one of our mining.submit requests
        bool fOk = (valResult.type() == bool_type) && valResult.get_bool();
        int64_t nAcc, nRej;
        {
            LOCK(cs);
            if (fOk)
                nSharesAccepted++;
            else
                nSharesRejected++;
            nAcc = nSharesAccepted;
            nRej = nSharesRejected;
        }
        LogPrintf("stratum: share %s (accepted %d / rejected %d)\n",
                  fOk ? "ACCEPTED" : "rejected", (int)nAcc, (int)nRej);
    }
}

bool CStratumClient::SubmitShare(const std::string& strJobId, const std::string& strExtraNonce2,
                                 const std::string& strNTime, const std::string& strNonce)
{
    // Miningcore param order (verified in the pool's BitcoinJobManager):
    // [worker, jobId, extraNonce2, nTime, nonce] — nTime/nonce exactly 8 hex chars.
    std::string strLine;
    {
        LOCK(cs_write);
        if (!pSocket)
            return false;
        strLine = strprintf(
            "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
            nSubmitId++, strUser, strJobId, strExtraNonce2, strNTime, strNonce);
        try {
            boost::asio::write(*static_cast<boost::asio::ip::tcp::socket*>(pSocket),
                               boost::asio::buffer(strLine));
        } catch (const std::exception& e) {
            LogPrintf("stratum: submit write failed: %s\n", e.what());
            return false;
        }
    }
    {
        LOCK(cs);
        nSharesSubmitted++;
    }
    LogPrint("stratum", "stratum: >>> %s", strLine);
    return true;
}

// Bitcoin difficulty-1 target (0x00000000ffff0000…) as a double; Miningcore's
// share test for this coin is diff1 / quark(header) >= 0.99 * vardiff
// (shareMultiplier is 1 in the pool's coin definition).
static const double DIFF1_TARGET = 65535.0 * std::pow(2.0, 208.0);

//! Stratum prevhash (dword-order-reversed RPC hash) → uint256 (internal LE),
//! per research/stratum-dialect-probe-2026-08-07.md: reversing the 8 words
//! yields the RPC display hex, which SetHex() then byte-reverses internally.
static bool ParsePrevHash(const std::string& strStratumHex, uint256& hashOut)
{
    if (strStratumHex.size() != 64)
        return false;
    std::string strRpcHex;
    for (int i = 7; i >= 0; i--)
        strRpcHex += strStratumHex.substr(i * 8, 8);
    hashOut.SetHex(strRpcHex);
    return true;
}

static uint32_t ParseHexBE32(const std::string& strHex)
{
    return (uint32_t)strtoul(strHex.c_str(), NULL, 16);
}

void CStratumClient::ThreadWorker(int nWorkerId)
{
    RenameThread("offerings-stratum-worker");
    LogPrintf("stratum: hash worker %d started\n", nWorkerId);

    uint32_t nEn2Counter = 0;

    while (!fShutdown) {
        // Snapshot the current work; anything missing → idle and retry.
        StratumJob job;
        std::string strEn1;
        int nEn2Size;
        double dDiff;
        int64_t nJobSeq;
        bool fReady;
        {
            LOCK(cs);
            job = currentJob;
            strEn1 = strExtraNonce1;
            nEn2Size = nExtraNonce2Size;
            dDiff = dDifficulty;
            nJobSeq = nJobsReceived;
            fReady = fAuthorized && !job.IsNull() && dDiff > 0.0 && !strEn1.empty();
        }
        if (!fReady) {
            MilliSleep(250);
            continue;
        }
        if (nEn2Size != 4) {
            // Layout below packs worker id + counter into exactly 4 bytes;
            // the pool advertises 4. Bail loudly rather than mis-mine.
            LogPrintf("stratum: unsupported extranonce2_size %d (worker %d idle)\n",
                      nEn2Size, nWorkerId);
            MilliSleep(5000);
            continue;
        }

        // Unique extranonce2 per (worker, rebuild): 1 byte worker id + 3 byte counter.
        uint32_t nEn2 = ((uint32_t)nWorkerId << 24) | (nEn2Counter++ & 0x00ffffff);
        std::string strEn2 = strprintf("%08x", nEn2);

        // coinbase = coinb1 ‖ extranonce1 ‖ extranonce2 ‖ coinb2, txid = sha256d
        std::vector<unsigned char> vchCoinbase = ParseHex(job.strCoinb1);
        std::vector<unsigned char> vchEn1 = ParseHex(strEn1);
        std::vector<unsigned char> vchEn2 = ParseHex(strEn2);
        std::vector<unsigned char> vchCoinb2 = ParseHex(job.strCoinb2);
        vchCoinbase.insert(vchCoinbase.end(), vchEn1.begin(), vchEn1.end());
        vchCoinbase.insert(vchCoinbase.end(), vchEn2.begin(), vchEn2.end());
        vchCoinbase.insert(vchCoinbase.end(), vchCoinb2.begin(), vchCoinb2.end());
        uint256 hashMerkleRoot = Hash(vchCoinbase.begin(), vchCoinbase.end());

        // Fold through the branch: root = sha256d(root ‖ branch[i]), branch
        // hashes arrive as raw-byte hex (no reversal — Miningcore convention).
        bool fBadBranch = false;
        for (unsigned int i = 0; i < job.vMerkleBranch.size(); i++) {
            std::vector<unsigned char> vchBranch = ParseHex(job.vMerkleBranch[i]);
            if (vchBranch.size() != 32) {
                fBadBranch = true;
                break;
            }
            uint256 hashBranch;
            memcpy(BEGIN(hashBranch), &vchBranch[0], 32);
            hashMerkleRoot = Hash(BEGIN(hashMerkleRoot), END(hashMerkleRoot),
                                  BEGIN(hashBranch), END(hashBranch));
        }
        if (fBadBranch) {
            LogPrintf("stratum: malformed merkle branch in job %s\n", job.strJobId);
            MilliSleep(1000);
            continue;
        }

        CBlockHeader header;
        header.nVersion = (int)ParseHexBE32(job.strVersion);
        if (!ParsePrevHash(job.strPrevHash, header.hashPrevBlock)) {
            LogPrintf("stratum: malformed prevhash in job %s\n", job.strJobId);
            MilliSleep(1000);
            continue;
        }
        header.hashMerkleRoot = hashMerkleRoot;
        header.nTime = ParseHexBE32(job.strNTime);
        header.nBits = ParseHexBE32(job.strNBits);
        header.nNonce = 0;

        // Share bound: quark(header) <= diff1/vardiff. Compared in doubles —
        // plenty for share screening; the pool revalidates every submit.
        const double dTargetBound = DIFF1_TARGET / dDiff;

        for (uint32_t nNonce = 0; !fShutdown; nNonce++) {
            header.nNonce = nNonce;
            uint256 hash = header.GetHash(false);
            if (hash.getdouble() <= dTargetBound) {
                LogPrintf("stratum: worker %d found share (job %s, nonce %08x, hash %s)\n",
                          nWorkerId, job.strJobId, nNonce, hash.GetHex().substr(0, 24));
                SubmitShare(job.strJobId, strEn2, job.strNTime, strprintf("%08x", nNonce));
            }
            if ((nNonce & 0xfff) == 0xfff) {
                bool fStale;
                {
                    LOCK(cs);
                    nHashCounter += 0x1000;
                    fStale = (nJobsReceived != nJobSeq);
                }
                if (fStale)
                    break; // re-snapshot: new job (and a fresh extranonce2)
            }
            if (nNonce == 0xffffffff)
                break; // nonce space exhausted: roll extranonce2
        }
    }
    LogPrintf("stratum: hash worker %d exiting\n", nWorkerId);
}

int64_t CStratumClient::GetSharesSubmitted() const
{
    LOCK(cs);
    return nSharesSubmitted;
}

int64_t CStratumClient::GetSharesAccepted() const
{
    LOCK(cs);
    return nSharesAccepted;
}

int64_t CStratumClient::GetSharesRejected() const
{
    LOCK(cs);
    return nSharesRejected;
}

double CStratumClient::GetHashRate() const
{
    LOCK(cs);
    int64_t nNow = GetTimeMillis();
    if (nHashRateTime == 0) {
        // first call: establish the baseline
        nHashRateTime = nNow;
        nHashRateCount = nHashCounter;
        return 0.0;
    }
    if (nNow - nHashRateTime >= 5000) {
        dHashRate = (double)(nHashCounter - nHashRateCount) * 1000.0 / (double)(nNow - nHashRateTime);
        nHashRateTime = nNow;
        nHashRateCount = nHashCounter;
    }
    return dHashRate;
}

bool StartStratumIfConfigured()
{
    std::string strEndpoint = GetArg("-stratum", "");
    if (strEndpoint.empty())
        return true;

    size_t colon = strEndpoint.rfind(':');
    if (colon == std::string::npos) {
        LogPrintf("stratum: invalid -stratum=%s (expected host:port)\n", strEndpoint);
        return false;
    }
    std::string strHost = strEndpoint.substr(0, colon);
    int nPort = atoi(strEndpoint.substr(colon + 1).c_str());
    std::string strUser = GetArg("-stratumuser", "");
    if (strHost.empty() || nPort <= 0 || nPort > 65535 || strUser.empty()) {
        LogPrintf("stratum: -stratum needs host:port and -stratumuser=<address>\n");
        return false;
    }

    int nThreads = (int)GetArg("-stratumthreads", 1);
    if (nThreads < 0 || nThreads > 64) {
        LogPrintf("stratum: invalid -stratumthreads=%d\n", nThreads);
        return false;
    }

    return StartStratum(strHost, nPort, strUser, nThreads);
}

bool StartStratum(const std::string& strHost, int nPort, const std::string& strUser,
                  int nThreads)
{
    LOCK(cs_stratum_control);
    StopStratum();
    g_pStratumClient = new CStratumClient();
    if (!g_pStratumClient->Start(strHost, nPort, strUser, nThreads)) {
        delete g_pStratumClient;
        g_pStratumClient = NULL;
        return false;
    }
    return true;
}

void StopStratum()
{
    LOCK(cs_stratum_control);
    if (g_pStratumClient) {
        g_pStratumClient->Stop();
        delete g_pStratumClient;
        g_pStratumClient = NULL;
    }
}
