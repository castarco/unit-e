// Copyright (c) 2018-2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <proposer/proposer_logic.h>

namespace proposer {

class LogicImpl final : public Logic {

 private:
  const Dependency<blockchain::Behavior> m_blockchain_behavior;
  const Dependency<staking::Network> m_network;
  const Dependency<staking::ActiveChain> m_active_chain;
  const Dependency<staking::StakeValidator> m_stake_validator;

 public:
  LogicImpl(
      const Dependency<blockchain::Behavior> blockchain_behavior,
      const Dependency<staking::Network> network,
      const Dependency<staking::ActiveChain> active_chain,
      const Dependency<staking::StakeValidator> stake_validator)
      : m_blockchain_behavior(blockchain_behavior),
        m_network(network),
        m_active_chain(active_chain),
        m_stake_validator(stake_validator) {}

  // An implementation of the Proof-of-Stake proposing/mining algorithm.
  //
  // foreach (utxo in wallet) {
  //   kernelhash = hash(prevBlock.stakeModifier ++ utxo.time ++ utxo.hash ++ utxo.n ++ blockTime)
  //   if (kernelhash < difficulty * utxo.value) {
  //     block.stakeModifier = hash(kernelhash ++ prevBlock.stakeModifier)
  //     propose(block);
  //     return;
  //   }
  // }
  //
  // The details as for how to calculate the kernel hash and check a valid kernel
  // are left up to the injectable staking::StakeValidator.
  //
  // The part of actually proposing (`propose(block)`) is left up to the caller
  // of this function (the `Proposer`, see proposer.cpp).
  boost::optional<proposer::EligibleCoin> TryPropose(const staking::CoinSet &eligible_coins) override {
    AssertLockHeld(m_active_chain->GetLock());

    const CBlockIndex *current_tip =
        m_active_chain->GetTip();
    if (!current_tip) {
      return boost::none;
    }

    const blockchain::Height current_height =
        m_active_chain->GetHeight();
    const blockchain::Height target_height =
        current_height + 1;

    const int64_t best_time = std::max(current_tip->GetMedianTimePast() + 1, m_network->GetTime());
    const blockchain::Time target_time =
        m_blockchain_behavior->CalculateProposingTimestampAfter(best_time);
    const blockchain::Difficulty target_difficulty =
        m_blockchain_behavior->CalculateDifficulty(target_height, *m_active_chain);

    uint32_t num_eligible_coins = 0;
    uint64_t max_stake = 0;
    uint256 best_kernel_hash;
    for (const staking::Coin &coin : eligible_coins) {
      ++num_eligible_coins;

      const uint256 kernel_hash = m_stake_validator->ComputeKernelHash(current_tip, coin, target_time);

      if (coin.GetAmount() > max_stake) {
        max_stake = coin.GetAmount();
        best_kernel_hash = kernel_hash;
      }

      if (!m_stake_validator->CheckKernel(coin.GetAmount(), kernel_hash, target_difficulty)) {
        if (m_blockchain_behavior->GetParameters().mine_blocks_on_demand) {
          LogPrint(BCLog::PROPOSING, "Letting artificial block generation succeed nevertheless (mine_blocks_on_demand=true)\n");
        } else {
          continue;
        }
      }

      const CAmount reward = m_blockchain_behavior->CalculateBlockReward(target_height);
      return {{coin,
               kernel_hash,
               reward,
               target_height,
               target_time,
               target_difficulty}};
    }

    {
      arith_uint256 target_value;
      bool is_negative;
      bool is_overflow;
      target_value.SetCompact(target_difficulty, &is_negative, &is_overflow);
      const arith_uint256 weight(static_cast<uint64_t>(max_stake));
      target_value *= weight;
      auto const kernel_hash_num = UintToArith256(best_kernel_hash);

      LogPrintf(
        "Unable to propose, max_stake=%i, num_coins=%i, height=%i, difficulty=%i, (target=%s kernel=%s)\n",
        max_stake, num_eligible_coins,
        target_height,
        target_difficulty,
        target_value.ToString(),
        kernel_hash_num.ToString()
      );
    }

    return boost::none;
  }
};

std::unique_ptr<Logic> Logic::New(
    const Dependency<blockchain::Behavior> blockchain_behavior,
    const Dependency<staking::Network> network,
    const Dependency<staking::ActiveChain> active_chain,
    const Dependency<staking::StakeValidator> stake_validator) {
  return std::unique_ptr<Logic>(new LogicImpl(blockchain_behavior, network, active_chain, stake_validator));
}

}  // namespace proposer
