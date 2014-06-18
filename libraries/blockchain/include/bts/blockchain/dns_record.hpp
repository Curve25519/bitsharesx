#pragma once

namespace bts { namespace blockchain {

    struct auction_data
    {
        string        domain_name;
        address       current_bidder;
        asset         current_bid;
        asset         required_bid;
    };

    struct domain_data
    {
        address        owner;
        string         domain_name;
        variant        value;
    };

}}; // bts::blockchain
