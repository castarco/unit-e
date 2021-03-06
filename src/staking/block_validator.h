// Copyright (c) 2018-2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef UNIT_E_STAKING_BLOCK_VALIDATOR_H
#define UNIT_E_STAKING_BLOCK_VALIDATOR_H

#include <better-enums/enum.h>
#include <better-enums/enum_set.h>
#include <blockchain/blockchain_behavior.h>
#include <dependency.h>
#include <primitives/block.h>
#include <staking/block_validation_info.h>
#include <staking/validation_result.h>
#include <uint256.h>

#include <boost/optional.hpp>

#include <cstdint>

namespace staking {

//! \brief A component for validating blocks and headers.
//!
//! This class is an interface.
//!
//! Design principles of the block validator:
//! - does not access the active chain or have any side effects.
//! - does not require any locks to be held.
//! - everything it needs to validate comes from the arguments passed to
//!   a function of from the currently active blockchain Behavior (which network).
//!
//! Since the previous call graph of validation functions was very hard to follow,
//! the relationship of the validation functions in the validator has been defined
//! in the following way:
//!
//! There are functions for validating:
//! (A) CBlockHeader
//! (B) CBlock
//!
//! And there are functions for validating:
//! (1) well-formedness (that is, values are in their proper place and look as they should)
//! (2) relation to the previous block
//!
//! A function of category (B) will always trigger the respective function from category (A)
//! first and continue only if that validated successfully.
//!
//! A function of category (2) will always trigger the respective function from category (1)
//! first and continue only if that validated successfully.
//!
//! All of these functions can be invoked passing a `BlockValidationInfo` pointer (which is
//! optional). If they are invoked with that they will track the state of validation and
//! don't do these checks again in case they have already been performed.
class BlockValidator {

 public:
  //! \brief checks that the block has the right structure, but nothing else.
  //!
  //! A well-formed block is supposed to follow the following structure:
  //! - at least one transaction (the coinbase transaction)
  //! - the coinbase transaction must be the first transaction
  //! - no other transaction maybe marked as coinbase transaction
  //!
  //! This function can be used to check the genesis block for well-formedness.
  //!
  //! The second parameter of this function is a reference to a BlockValidationInfo
  //! object which tracks validation information across invocations of different
  //! validation functions like CheckBlock, ContextualCheckBlock, CheckStake.
  //!
  //! Postconditions when invoked as `blockValidationResult = CheckBlock(block, blockValidationInfo)`:
  //! - (bool) blockValidationResult == (bool) blockValidationInfo.GetCheckBlockStatus()
  //! - !blockValidationResult || block.vtx.size() >= 1
  //! - !blockValidationResult || block.vtx[0].GetType() == +TxType::COINBASE
  virtual BlockValidationResult CheckBlock(
      const CBlock &block,       //!< [in] The block to check.
      BlockValidationInfo *info  //!< [in,out] Access to the validation info for this block (optional, nullptr may be passed).
      ) const = 0;

  //! \brief checks the block with respect to its preceding block.
  //!
  //! This function can not be used to check the genesis block, as it does not have
  //! a preceding block.
  virtual BlockValidationResult ContextualCheckBlock(
      const CBlock &block,             //!< [in] The block to check.
      const CBlockIndex &block_index,  //!< [in] The block index entry of the preceding block.
      blockchain::Time adjusted_time,  //!< [in] The adjusted network time at the point in time to check for.
      BlockValidationInfo *info        //!< [in,out] Access to the validation info for this block (optional, nullptr may be passed).
      ) const = 0;

  //! \brief checks that the block header has the right structure, but nothing else.
  //!
  //! This function can be used to check the genssis block's header for well-formedness.
  virtual BlockValidationResult CheckBlockHeader(
      const CBlockHeader &block_header,
      BlockValidationInfo *info) const = 0;

  //! \brief checks the block header with resepect to its preceding block.
  //!
  //! This function can not be used to check the genesis block's header, as that one
  //! does not have a preceding block.
  virtual BlockValidationResult ContextualCheckBlockHeader(
      const CBlockHeader &block_header,  //!< [in] The header to check
      const CBlockIndex &block_index,    //!< [in] The block index entry for the previous block.
      blockchain::Time time,             //!< [in] The adjusted network time at the point in time to check for.
      BlockValidationInfo *info          //!< [in,out] Access to the validation info for this block (optional, nullptr may be passed).
      ) const = 0;

  //! \brief checks the coinbase transaction to be well-formed
  //!
  //! A coinbase transaction is expected to have at least two inputs:
  //! - the meta input carrying height and snapshot hash at vin[0]
  //! - the staking input at vin[1]
  virtual BlockValidationResult CheckCoinbaseTransaction(
      const CBlock &block,             //!< [in] The block that contains this coinbase transaction
      const CTransaction &coinbase_tx  //!< [in] The coinbase transaction to check
      ) const = 0;

  //! \brief checks a transaction to be well-formed
  virtual BlockValidationResult CheckTransaction(
      const CTransaction &tx  //!< [in] The transaction to check
      ) const = 0;

  virtual ~BlockValidator() = default;

  static std::unique_ptr<BlockValidator> New(Dependency<blockchain::Behavior>);
};

//! \brief Block Validator that handles orchestration logic only
//!
//! This class is extracted so that it can be unit tested and check the interactions between
//! calls if orchestrated using staking::BlockValidationInfo.
class AbstractBlockValidator : public BlockValidator {

 protected:
  virtual BlockValidationResult CheckBlockHeaderInternal(
      const CBlockHeader &block_header) const = 0;

  virtual BlockValidationResult ContextualCheckBlockHeaderInternal(
      const CBlockHeader &block_header,
      blockchain::Time adjusted_time,
      const CBlockIndex &previous_block) const = 0;

  virtual BlockValidationResult CheckBlockInternal(
      const CBlock &block,
      blockchain::Height *height_out,
      uint256 *snapshot_hash_out) const = 0;

  virtual BlockValidationResult ContextualCheckBlockInternal(
      const CBlock &block,
      const CBlockIndex &prev_block,
      const BlockValidationInfo &validation_info) const = 0;

 public:
  BlockValidationResult CheckBlock(
      const CBlock &block,
      BlockValidationInfo *block_validation_info) const final;

  BlockValidationResult ContextualCheckBlock(
      const CBlock &block,
      const CBlockIndex &prev_block,
      blockchain::Time adjusted_time,
      BlockValidationInfo *block_validation_info) const final;

  BlockValidationResult CheckBlockHeader(
      const CBlockHeader &block_header,
      BlockValidationInfo *block_validation_info) const final;

  BlockValidationResult ContextualCheckBlockHeader(
      const CBlockHeader &block_header,
      const CBlockIndex &prev_block,
      blockchain::Time adjusted_time,
      BlockValidationInfo *block_validation_info) const final;
};

}  // namespace staking

#endif  //UNIT_E_STAKE_VALIDATOR_H
