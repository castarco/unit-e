// Copyright (c) 2018-2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/validation.h>
#include <esperanza/finalizationstate.h>
#include <finalization/p2p.h>
#include <net.h>
#include <netmessagemaker.h>
#include <validation.h>

namespace finalization {
namespace p2p {

std::string CommitsLocator::ToString() const {
  return strprintf("Locator(start=%s, stop=%s)", util::to_string(start), stop.GetHex());
}

namespace {
const CBlockIndex *FindMostRecentStart(const CChain &chain, const CommitsLocator &locator) {
  const auto *const state = esperanza::FinalizationState::GetState();
  const CBlockIndex *last = nullptr;
  for (const uint256 &h : locator.start) {
    const auto it = mapBlockIndex.find(h);
    if (it == mapBlockIndex.end()) {
      LogPrint(BCLog::FINALIZATION, "Block not found: %s", h.GetHex());
      return last;
    }
    CBlockIndex *const pindex = it->second;
    if (last == nullptr) {  // first hash in `start` must be finalized
      if (!state->IsFinalizedCheckpoint(pindex->nHeight)) {
        LogPrint(BCLog::FINALIZATION, "The first hash in locator must be finalized checkpoint: %s (%d)\n",
                 h.GetHex(), pindex->nHeight);
        return nullptr;
      }
      last = pindex;
      assert(chain.Contains(pindex));
    } else if (pindex->nHeight > last->nHeight && chain.Contains(pindex)) {
      last = pindex;
    } else {
      break;
    }
  }
  return last;
}

const CBlockIndex *FindStop(const CommitsLocator &locator) {
  if (locator.stop.IsNull()) {
    return nullptr;
  }
  const auto it = mapBlockIndex.find(locator.stop);
  if (it == mapBlockIndex.end()) {
    LogPrint(BCLog::FINALIZATION, "Hash %s not found, fallback to stop=0x0\n", locator.stop.GetHex());
    return nullptr;
  }
  return it->second;
}

HeaderAndCommits FindHeaderAndCommits(const CBlockIndex *pindex, const Consensus::Params &params) {
  HeaderAndCommits hc(pindex->GetBlockHeader());
  if (pindex->commits.get_ptr() != nullptr) {
    hc.commits = pindex->commits.get();
    return hc;
  }
  if (!(pindex->nStatus & BLOCK_HAVE_DATA)) {
    LogPrintf("%s has no data. It's on the main chain, so this shouldn't happen. Stopping.\n",
              pindex->GetBlockHash().GetHex());
    assert(not("No data on the main chain"));
  }
  const std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
  if (!ReadBlockFromDisk(*pblock, pindex, params)) {
    assert(not("Cannot load block from the disk"));
  }
  for (const auto &tx : pblock->vtx) {
    if (tx->IsFinalizationTransaction()) {
      hc.commits.push_back(tx);
    }
  }
  return hc;
}
}  // namespace

bool ProcessGetCommits(CNode *node, const CommitsLocator &locator, const CNetMsgMaker &msgMaker,
                       const CChainParams &chainparams) {
  const CBlockIndex *pindex = FindMostRecentStart(chainActive, locator);
  if (pindex == nullptr) {
    return error("%s: cannot find start point in locator: %s", __func__, locator.ToString());
  }
  const CBlockIndex *const stop = FindStop(locator);
  const auto *const state = esperanza::FinalizationState::GetState();
  CommitsResponse r;
  do {
    pindex = chainActive.Next(pindex);
    if (pindex == nullptr) {
      r.status = CommitsResponse::Status::TipReached;
      break;
    }
    // UNIT-E detect message overflow
    r.data.emplace_back(FindHeaderAndCommits(pindex, chainparams.GetConsensus()));
  } while (pindex != stop && !state->IsFinalizedCheckpoint(pindex->nHeight));
  LogPrint(BCLog::NET, "Send %d headers+commits, status = %d\n",
           r.data.size(), static_cast<uint8_t>(r.status));
  g_connman->PushMessage(node, msgMaker.Make(NetMsgType::COMMITS, r));
  return true;
}

bool ProcessNewCommits(const CommitsResponse &msg, const CChainParams &chainparams,
                       CValidationState &validation_state, uint256 *failed_block_out) {
  const auto err = [&](int code, const std::string &str, const uint256 &block) {
    if (failed_block_out != nullptr) {
      *failed_block_out = block;
    }
    return validation_state.DoS(code, false, REJECT_INVALID, str);
  };
  for (const auto &d : msg.data) {
    // UNIT-E: Check commits merkle root after it is added
    for (const auto &c : d.commits) {
      if (!c->IsFinalizationTransaction()) {
        return err(100, "bad-non-commit", d.header.GetHash());
      }
    }
  }
  for (const auto &d : msg.data) {
    CBlockIndex *pindex = nullptr;
    if (!AcceptBlockHeader(d.header, validation_state, chainparams, &pindex)) {
      return false;
    }
    assert(pindex != nullptr);
    if (!pindex->IsValid(BLOCK_VALID_TREE)) {
      return err(100, "bad-block-index", d.header.GetHash());
    }
    pindex->ResetCommits(d.commits);
    // UNIT-E: Validate commits transactions and reconstruct finalization state
  }
  // UNIT-E: Implement in two further steps: full-sync and PUSH
  switch (msg.status) {
    case CommitsResponse::Status::StopOrFinReached:
      // UNIT-E: Request next bulk
      break;
    case CommitsResponse::Status::TipReached:
      // UNIT-E: Trigger fork choice if reconstructed finalization state is better than current one
      break;
    case CommitsResponse::Status::LengthExceeded:
      // UNIT-E: Wait the next message
      break;
  }
  return true;
}

}  // namespace p2p
}  // namespace finalization