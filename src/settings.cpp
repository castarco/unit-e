// Copyright (c) 2018-2019 The Unit-e developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <settings.h>

#include <key_io.h>
#include <dependency.h>
#include <shutdown.h>

std::unique_ptr<Settings> Settings::New(
    Dependency<::ArgsManager> args,
    Dependency<blockchain::Behavior> blockchain_behavior) {

  std::unique_ptr<Settings> settings = MakeUnique<Settings>(blockchain_behavior->GetDefaultSettings());

  settings->node_is_proposer =
      args->GetBoolArg("-proposing", settings->node_is_proposer);

  settings->node_is_validator =
      args->GetBoolArg("-validating", settings->node_is_validator);

  settings->stake_combine_maximum =
      args->GetArg("-stakecombinemaximum", settings->stake_combine_maximum);

  settings->stake_split_threshold =
      args->GetArg("-stakesplitthreshold", settings->stake_split_threshold);

  const std::string reward_address = args->GetArg("-rewardaddress", "");
  if (!reward_address.empty()) {
    CTxDestination reward_dest = DecodeDestination(reward_address);
    if (IsValidDestination(reward_dest)) {
      settings->reward_destination = std::move(reward_dest);
    } else {
      settings->reward_destination = boost::none;
      LogPrintf("%s: -rewardaddress: Invalid address provided %s\n", __func__, reward_address);
      throw std::runtime_error("Invalid -rewardaddress");
    }
  }

  if (args->IsArgSet("-datadir")) {
    const fs::path path = fs::system_complete(args->GetArg("-datadir", ""));
    if (fs::is_directory(path)) {
      settings->base_data_dir = path;
    }
  }
  settings->data_dir = settings->base_data_dir / blockchain_behavior->GetParameters().data_dir_suffix;

  settings->finalizer_vote_from_epoch_block_number =
      args->GetArg("-finalizervotefromepochblocknumber", settings->finalizer_vote_from_epoch_block_number);

  return settings;
}
