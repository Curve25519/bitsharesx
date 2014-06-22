#pragma once
#include <bts/blockchain/types.hpp>

namespace bts { namespace blockchain {

    struct auction_record
    {
        string                 domain_name;
        address                current_bidder;
        asset                  current_bid;
        asset                  required_bid;
        fc::time_point_sec     last_bid_time; 
    };

    struct domain_record
    {
        string                 domain_name;
        address                owner;
        variant                value;
        fc::time_point_sec     last_update;
    };

}}; // bts::blockchain
