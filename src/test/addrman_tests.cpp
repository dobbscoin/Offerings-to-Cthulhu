// Copyright (c) 2026 The Offerings developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addrman.h"
#include "netbase.h"

#include <boost/test/unit_test.hpp>

using namespace std;

// Expose the protected internals under test. Create() bypasses Add_'s
// routability filter, which is exactly how pre-#44 caches ended up holding
// non-routable entries — the state issue #51 guards against.
class CAddrManTest : public CAddrMan
{
public:
    using CAddrMan::Create;
    using CAddrMan::GetAddr_;
};

static CAddress RoutableAddr(int i)
{
    // 250.0.0.0/8 is plain routable IPv4 (same convention as upstream tests)
    CAddress addr(CService(strprintf("250.1.%d.%d", (i >> 8) & 0xff, i & 0xff), 20000));
    addr.nTime = GetAdjustedTime(); // fresh, so nothing else filters it
    return addr;
}

BOOST_AUTO_TEST_SUITE(addrman_tests)

BOOST_AUTO_TEST_CASE(getaddr_skips_nonroutable)
{
    CAddrManTest addrman;
    CNetAddr source("252.2.2.2");

    // 100 routable + 50 non-routable (CGNAT per #44, RFC1918, loopback)
    for (int i = 0; i < 100; i++)
        BOOST_REQUIRE(addrman.Create(RoutableAddr(i), source) != NULL);
    for (int i = 0; i < 30; i++) {
        CAddress addr(CService(strprintf("100.87.%d.%d", (i >> 8) & 0xff, i & 0xff), 20000));
        addr.nTime = GetAdjustedTime();
        BOOST_CHECK(!addr.IsRoutable()); // sanity: RFC 6598 marked non-routable by #44
        BOOST_REQUIRE(addrman.Create(addr, source) != NULL);
    }
    for (int i = 0; i < 19; i++) {
        CAddress addr(CService(strprintf("192.168.0.%d", i + 1), 20000));
        addr.nTime = GetAdjustedTime();
        BOOST_REQUIRE(addrman.Create(addr, source) != NULL);
    }
    {
        CAddress addr(CService("127.0.0.1", 20000));
        addr.nTime = GetAdjustedTime();
        BOOST_REQUIRE(addrman.Create(addr, source) != NULL);
    }
    BOOST_CHECK_EQUAL(addrman.size(), 150);

    std::vector<CAddress> vAddr;
    addrman.GetAddr_(vAddr);

    // 1) Nothing non-routable is ever served
    for (unsigned int i = 0; i < vAddr.size(); i++)
        BOOST_CHECK_MESSAGE(vAddr[i].IsRoutable(),
                            "getaddr served non-routable " + vAddr[i].ToString());

    // 2) Bookkeeping: skipped entries must not shrink the reply below the
    //    intended selection size (23% of 150 = 34, well under the 100
    //    routable entries available)
    unsigned int nExpect = ADDRMAN_GETADDR_MAX_PCT * 150 / 100;
    BOOST_CHECK_EQUAL(vAddr.size(), nExpect);
}

BOOST_AUTO_TEST_CASE(getaddr_all_nonroutable_returns_empty)
{
    CAddrManTest addrman;
    CNetAddr source("252.2.2.2");

    for (int i = 0; i < 40; i++) {
        CAddress addr(CService(strprintf("10.0.%d.%d", (i >> 8) & 0xff, i & 0xff), 20000));
        addr.nTime = GetAdjustedTime();
        BOOST_REQUIRE(addrman.Create(addr, source) != NULL);
    }

    std::vector<CAddress> vAddr;
    addrman.GetAddr_(vAddr);
    BOOST_CHECK_EQUAL(vAddr.size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
