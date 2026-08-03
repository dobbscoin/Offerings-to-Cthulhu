// Copyright (c) 2026 The Offerings developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Emergency-difficulty gate tests (issue #59). Exercises the shared
// eligibility gates and the miner-side target selection against mainnet
// params (the test fixture's default network).

#include "chainparams.h"
#include "core.h"
#include "main.h"
#include "pow.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(pow_tests)

static CBlockIndex MakePrev(int nHeight, unsigned int nTime)
{
    CBlockIndex prev;
    prev.nHeight = nHeight;
    prev.nTime = nTime;
    prev.pprev = NULL;
    return prev;
}

BOOST_AUTO_TEST_CASE(emergency_eligibility_gates)
{
    const unsigned int T0 = 1754000000;

    // Post-window mainnet height: eligible strictly past the 1h gap.
    CBlockIndex prev = MakePrev(1070186, T0);
    BOOST_CHECK(!EmergencyDifficultyEligible(&prev, (int64_t)T0 + EMERGENCY_DIFFICULTY_GAP));      // exactly 1h: no
    BOOST_CHECK( EmergencyDifficultyEligible(&prev, (int64_t)T0 + EMERGENCY_DIFFICULTY_GAP + 1));  // 1h+1s: yes
    BOOST_CHECK(!EmergencyDifficultyEligible(&prev, (int64_t)T0 + 30));                            // fresh tip: no

    // NULL prev never qualifies.
    BOOST_CHECK(!EmergencyDifficultyEligible(NULL, (int64_t)T0 + 7200));

    // Pre-activation height gate.
    prev = MakePrev(HARDFORK_EMERGENCY_DIFF_MAIN_OFF - 2, T0);  // next height = fork - 1
    BOOST_CHECK(!EmergencyDifficultyEligible(&prev, (int64_t)T0 + 7200));
    prev = MakePrev(HARDFORK_EMERGENCY_DIFF_MAIN_OFF - 1, T0);  // next height = fork
    BOOST_CHECK( EmergencyDifficultyEligible(&prev, (int64_t)T0 + 7200));

    // Conclave signed-mining window is hard-skipped, inclusive both ends.
    prev = MakePrev(Params().SignedWindowStart() - 1, T0);      // next height = window start
    BOOST_CHECK(!EmergencyDifficultyEligible(&prev, (int64_t)T0 + 7200));
    prev = MakePrev(Params().OpenMiningHeight() - 1, T0);       // next height = window end
    BOOST_CHECK(!EmergencyDifficultyEligible(&prev, (int64_t)T0 + 7200));
    prev = MakePrev(Params().OpenMiningHeight(), T0);           // next height = first open block
    BOOST_CHECK( EmergencyDifficultyEligible(&prev, (int64_t)T0 + 7200));
}

BOOST_AUTO_TEST_CASE(mining_target_uses_emergency_bits)
{
    const unsigned int T0 = 1754000000;
    CBlockIndex prev = MakePrev(1070186, T0);
    prev.nBits = 0x1d00c3cf;  // the strand the 2026-08-03 incident left behind

    CBlockHeader header;
    header.nTime = T0 + EMERGENCY_DIFFICULTY_GAP + 1;

    // Eligible: the mining target collapses to powLimit...
    BOOST_CHECK_EQUAL(GetNextWorkRequiredForMining(&prev, &header),
                      Params().ProofOfWorkLimit().GetCompact());

    // ...and a block built that way passes the validation exemption.
    header.nBits = Params().ProofOfWorkLimit().GetCompact();
    BOOST_CHECK(IsEmergencyDifficultyBlock(header, &prev));

    // A better-than-powLimit block inside the gap must NOT claim the exemption.
    header.nBits = 0x1d00c3cf;
    BOOST_CHECK(!IsEmergencyDifficultyBlock(header, &prev));

    // Not eligible (fresh tip): validation exemption stays closed even at powLimit.
    header.nTime = T0 + 30;
    header.nBits = Params().ProofOfWorkLimit().GetCompact();
    BOOST_CHECK(!IsEmergencyDifficultyBlock(header, &prev));
}

BOOST_AUTO_TEST_SUITE_END()
