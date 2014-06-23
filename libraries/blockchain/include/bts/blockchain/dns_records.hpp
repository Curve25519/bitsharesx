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

    typedef fc::optional<domain_record>    odomain_record;
    typedef fc::optional<auction_record>   oauction_record;

}}; // bts::blockchain

FC_REFLECT( bts::blockchain::auction_record, (domain_name)(current_bidder)(current_bid)(required_bid)(last_bid_time) );
FC_REFLECT( bts::blockchain::domain_record, (domain_name)(owner)(value)(last_update) );
