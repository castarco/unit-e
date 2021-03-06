#!/usr/bin/env python3
# Copyright (c) 2019 The Unit-e developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test the getparameters RPC.

Showcases how to pass chainparams to nodes and inject a new
genesis block including funds (for testing purposes). Also
checks that loading -customchainparams actually works."""
from test_framework.messages import UNIT
from test_framework.test_framework import (UnitETestFramework)
from test_framework.util import assert_equal


class GetParametersTest (UnitETestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [
            ["-proposing=0"],
            ["-proposing=1"]
        ]
        self.customchainparams = [
            {"block_time_seconds": 24,
             "block_stake_timestamp_interval_seconds": 48,
             "genesis_block": {
                 "p2wpkh_funds": [
                     {"amount": 7500 * UNIT, "pub_key_hash": "c2c28cd4df085d164ea0e4a2f8f9c5a4fbe86487"},
                     {"amount": 1500 * UNIT, "pub_key_hash": "4cc1c8059ce6e8e0124f3cc9676fbc985e68a4a0"},
                     {"amount": 2000 * UNIT, "pub_key_hash": "b99b83c1cea07c27a743d0440b698a7d59f88e08"}
                 ],
                 "p2wsh_funds": [
                     {"amount": 2500 * UNIT, "script_hash": "9d65e6fd035a643956361a3e5b2084cd8c10e07a5438c9ca1128017d4a02d185"}
                 ]
             }},
            {"network_name": "qualityland"},
            {"stake_maturity": 7}
        ]

    def setup_network(self):
        # start nodes
        self.setup_nodes()
        # do nothing further (overridden method connects nodes, we don't want that)
        # as they would discover their mismatching genesis blocks
        pass

    def run_test (self):
        params = [self.nodes[i].getchainparams() for i in range(0, self.num_nodes)]

        assert_equal(params[0]['block_time_seconds'], 24)
        assert_equal(params[0]['block_stake_timestamp_interval_seconds'], 48)
        assert_equal(len(params[0]['genesis_block']['p2wpkh_funds']), 3)
        assert_equal(params[0]['genesis_block']['p2wpkh_funds'][0]['amount'], 7500 * UNIT)
        assert_equal(params[0]['genesis_block']['p2wpkh_funds'][0]['pub_key_hash'], 'c2c28cd4df085d164ea0e4a2f8f9c5a4fbe86487')
        assert_equal(params[0]['genesis_block']['p2wpkh_funds'][1]['amount'], 1500 * UNIT)
        assert_equal(params[0]['genesis_block']['p2wpkh_funds'][1]['pub_key_hash'], '4cc1c8059ce6e8e0124f3cc9676fbc985e68a4a0')
        assert_equal(params[0]['genesis_block']['p2wpkh_funds'][2]['amount'], 2000 * UNIT)
        assert_equal(params[0]['genesis_block']['p2wpkh_funds'][2]['pub_key_hash'], 'b99b83c1cea07c27a743d0440b698a7d59f88e08')
        assert_equal(len(params[0]['genesis_block']['p2wsh_funds']), 1)
        assert_equal(params[0]['genesis_block']['p2wsh_funds'][0]['amount'], 2500 * UNIT)
        assert_equal(params[0]['genesis_block']['p2wsh_funds'][0]['script_hash'], '9d65e6fd035a643956361a3e5b2084cd8c10e07a5438c9ca1128017d4a02d185')
        assert_equal(params[1]['network_name'], "qualityland")
        assert_equal(params[2]['stake_maturity'], 7)

if __name__ == '__main__':
    GetParametersTest().main ()
