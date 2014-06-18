#pragma once

namespace bts { namespace blockchain {
    struct update_domain_data_operation
    {
        static const operation_type_enum type;
        address      owner;
        string       domain_name;
        variant      value;
    };

    struct update_auction_data_operation
    {
        enum auction_update_type
        {
            sell = 0,
            bid = 1
        }
        string      domain_name;
        address     bidder;
        asset       bid;
    }

}}; // bts::blockchain
