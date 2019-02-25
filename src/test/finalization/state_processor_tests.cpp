// Copyright (c) 2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <finalization/state_processor.h>
#include <finalization/state_repository.h>
#include <esperanza/finalizationstate.h>
#include <esperanza/adminparams.h>
#include <test/test_unite.h>
#include <test/test_unite_mocks.h>

#include <boost/test/unit_test.hpp>

namespace {
class Fixture {
 public:
  static constexpr blockchain::Height epoch_length = 5;

  Fixture()
    : m_repo(finalization::StateRepository::New(&m_chain)),
      m_proc(finalization::StateProcessor::New(m_repo.get())) {
    m_finalization_params = Params().GetFinalization();
    m_finalization_params.epoch_length = epoch_length;
    m_admin_params = Params().GetAdminParams();
    m_repo->Reset(m_finalization_params, m_admin_params);
    m_chain.block_at_height = [this](blockchain::Height h) -> CBlockIndex * {
      auto const it = this->m_block_heights.find(h);
      BOOST_REQUIRE(it != this->m_block_heights.end());
      return it->second;
    };
  }

  CBlockIndex &CreateBlockIndex() {
    const auto height = FindNextHeight();
    const auto ins_res = m_block_indexes.emplace(uint256S(std::to_string(height)), CBlockIndex());
    CBlockIndex &index = ins_res.first->second;
    index.nHeight = height;
    index.phashBlock = &ins_res.first->first;
    index.pprev = m_chain.tip;
    m_chain.tip = &index;
    m_block_heights[index.nHeight] = &index;
    return index;
  }

  bool ProcessNewCommits(const CBlockIndex &block_index) {
    return m_proc->ProcessNewCommits(block_index, {});
  }

  bool ProcessNewTipCandidate(const CBlockIndex &block_index) {
    return m_proc->ProcessNewTipCandidate(block_index, CBlock());
  }

  bool ProcessNewTip(const CBlockIndex &block_index) {
    return m_proc->ProcessNewTip(block_index, CBlock());
  }

  void AddBlock() {
    const auto &block_index = CreateBlockIndex();
    const bool process_res = ProcessNewTip(block_index);
    BOOST_REQUIRE(process_res);
  }

  void AddBlocks(size_t amount) {
    for (size_t i = 0; i < amount; ++i) {
      AddBlock();
    }
  }

  const esperanza::FinalizationState *GetState(const blockchain::Height h) {
    return m_repo->Find(*m_chain.AtHeight(h));
  }

  const esperanza::FinalizationState *GetState(const CBlockIndex &block_index) {
    return m_repo->Find(block_index);
  }

 private:
  blockchain::Height FindNextHeight() {
    if (m_chain.tip == nullptr) {
      return 0;
    } else {
      return m_chain.GetTip()->nHeight + 1;
    }
  }

  esperanza::FinalizationParams m_finalization_params;
  esperanza::AdminParams m_admin_params;
  std::map<uint256, CBlockIndex> m_block_indexes;
  std::map<blockchain::Height, CBlockIndex *> m_block_heights; // m_block_index owns these block indexes
  mocks::ActiveChainMock m_chain;
  std::unique_ptr<finalization::StateRepository> m_repo;
  std::unique_ptr<finalization::StateProcessor> m_proc;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(state_processor_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(trimming) {
  Fixture fixture;
  BOOST_REQUIRE(fixture.epoch_length == 5);

  // Generate first epoch
  fixture.AddBlocks(5);

  // Check, all states presented in the repository
  BOOST_CHECK(fixture.GetState(0) != nullptr);
  BOOST_CHECK(fixture.GetState(1) != nullptr);
  BOOST_CHECK(fixture.GetState(2) != nullptr);
  BOOST_CHECK(fixture.GetState(3) != nullptr);
  BOOST_CHECK(fixture.GetState(4) != nullptr);

  // Check, states are different
  for (blockchain::Height h1 = 0; h1 < 5; ++h1) {
    for (blockchain::Height h2 = 0; h2 <= h1; ++h2) {
      const auto lhs = fixture.GetState(h1);
      const auto rhs = fixture.GetState(h2);
      BOOST_CHECK(lhs != nullptr);
      BOOST_CHECK(rhs != nullptr);
      BOOST_CHECK_EQUAL(lhs == rhs, h1 == h2);
    }
  }

  // Generate one more block, trigger finalization of previous epoch
  fixture.AddBlocks(1);

  // Now epoch 1 is finalized, check old states disappear from the repository
  BOOST_CHECK(fixture.GetState(blockchain::Height(0)) != nullptr);  // genesis
  BOOST_CHECK(fixture.GetState(1) == nullptr);
  BOOST_CHECK(fixture.GetState(2) == nullptr);
  BOOST_CHECK(fixture.GetState(3) == nullptr);
  BOOST_CHECK(fixture.GetState(4) != nullptr);  // finalized checkpoint
  BOOST_CHECK(fixture.GetState(5) != nullptr);  // first block of new epoch

  // Complete current epoch
  fixture.AddBlocks(4);

  // Check, new states are in the repository
  BOOST_CHECK(fixture.GetState(4) != nullptr);
  BOOST_CHECK(fixture.GetState(5) != nullptr);
  BOOST_CHECK(fixture.GetState(9) != nullptr);

  // Generate next epoch. We haven't reached finalization yet.
  fixture.AddBlocks(5);
  BOOST_CHECK(fixture.GetState(4) != nullptr);
  BOOST_CHECK(fixture.GetState(5) != nullptr);
  BOOST_CHECK(fixture.GetState(9) != nullptr);

  // Generate one more block, trigger finalization of the first epoch
  fixture.AddBlocks(1);

  BOOST_CHECK(fixture.GetState(4) == nullptr);
  BOOST_CHECK(fixture.GetState(8) == nullptr);
  BOOST_CHECK(fixture.GetState(9) != nullptr);
  BOOST_CHECK(fixture.GetState(10) != nullptr);
}

BOOST_AUTO_TEST_CASE(states_workflow) {
  Fixture fixture;
  BOOST_REQUIRE(fixture.epoch_length == 5);

  // Generate first epoch
  fixture.AddBlocks(5);

  bool ok = false;
  const auto &block_index = fixture.CreateBlockIndex();

  // Process state from commits. It't not confirmed yet, finalization shouldn't happen.
  ok = fixture.ProcessNewCommits(block_index);
  BOOST_REQUIRE(ok);
  BOOST_CHECK(fixture.GetState(block_index)->GetInitStatus() == esperanza::FinalizationState::FROM_COMMITS);
  BOOST_CHECK(fixture.GetState(1) != nullptr);

  // Process the same state from the block, it must be confirmed now. As it's not yet considered as
  // a prt of the main chain, finalization shouldn't happen.
  ok = fixture.ProcessNewTipCandidate(block_index);
  BOOST_REQUIRE(ok);
  BOOST_CHECK(fixture.GetState(block_index)->GetInitStatus() == esperanza::FinalizationState::COMPLETED);
  BOOST_CHECK(fixture.GetState(1) != nullptr);

  // Process the same state from the block and consider it as a part of the main chain so that expect
  // finalization and trimming the repository.
  ok = fixture.ProcessNewTip(block_index);
  BOOST_REQUIRE(ok);
  BOOST_CHECK(fixture.GetState(block_index)->GetInitStatus() == esperanza::FinalizationState::COMPLETED);
  BOOST_CHECK(fixture.GetState(1) == nullptr);

  // Generate two more indexes
  const auto &b1 = fixture.CreateBlockIndex();
  const auto &b2 = fixture.CreateBlockIndex();

  // Try to process new state for b2. This should fail due to we haven't processed state for b1 yet.
  ok = fixture.ProcessNewCommits(b2);
  BOOST_CHECK_EQUAL(ok, false);
  ok = fixture.ProcessNewTipCandidate(b2);
  BOOST_CHECK_EQUAL(ok, false);
  ok = fixture.ProcessNewTip(b2);
  BOOST_CHECK_EQUAL(ok, false);

  // Process b1 state from commits and try to process b2 from block. This must fail due to we can't
  // confirm state that based on unconfirmed one.
  ok = fixture.ProcessNewCommits(b1);
  BOOST_REQUIRE(ok);
  ok = fixture.ProcessNewTipCandidate(b2);
  BOOST_CHECK_EQUAL(ok, false);
  ok = fixture.ProcessNewTip(b2);
  BOOST_CHECK_EQUAL(ok, false);

  // Now we can process b2 from commits and then from the block (it's what we do in snapshot sync).
  ok = fixture.ProcessNewCommits(b2);
  BOOST_CHECK_EQUAL(ok, true);
  ok = fixture.ProcessNewTip(b2);
  BOOST_CHECK_EQUAL(ok, true);

  // Process next block as usual
  fixture.AddBlocks(1);
}

BOOST_AUTO_TEST_SUITE_END()