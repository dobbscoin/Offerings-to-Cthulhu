// Copyright (c) 2026 The Offerings Conclave
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stratum.h"

#include "util.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

#include <boost/asio.hpp>
#include <boost/thread.hpp>

using namespace json_spirit;
using namespace std;

CStratumClient* g_pStratumClient = NULL;

CStratumClient::CStratumClient()
    : nPort(0),
      fConnected(false),
      fAuthorized(false),
      dDifficulty(0.0),
      nExtraNonce2Size(0),
      nJobsReceived(0),
      pthreadClient(NULL),
      fShutdown(false),
      nSocketFd(-1)
{
}

CStratumClient::~CStratumClient()
{
    Stop();
}

bool CStratumClient::Start(const std::string& strHostIn, int nPortIn, const std::string& strUserIn)
{
    if (pthreadClient)
        return false;
    strHost = strHostIn;
    nPort = nPortIn;
    strUser = strUserIn;
    fShutdown = false;
    pthreadClient = new boost::thread(boost::bind(&CStratumClient::ThreadStratum, this));
    return true;
}

void CStratumClient::Stop()
{
    fShutdown = true;
    // Unblock any blocking read by shutting the socket under it.
    int fd = nSocketFd;
    if (fd != -1) {
#ifdef WIN32
        closesocket(fd);
#else
        ::shutdown(fd, 2 /* SHUT_RDWR */);
#endif
    }
    if (pthreadClient) {
        pthreadClient->join();
        delete pthreadClient;
        pthreadClient = NULL;
    }
}

bool CStratumClient::IsRunning() const { return pthreadClient != NULL; }

bool CStratumClient::IsConnected() const
{
    LOCK(cs);
    return fConnected;
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
        nSocketFd = (int)socket.native_handle();

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
        nSocketFd = -1;
    } catch (const std::exception& e) {
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
    }
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

    g_pStratumClient = new CStratumClient();
    return g_pStratumClient->Start(strHost, nPort, strUser);
}

void StopStratum()
{
    if (g_pStratumClient) {
        g_pStratumClient->Stop();
        delete g_pStratumClient;
        g_pStratumClient = NULL;
    }
}
