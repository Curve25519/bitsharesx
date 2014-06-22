#pragma once

namespace bts { namespace blockchain {
    struct update_domain_operation
    {
        update_domain_operation():domain_name(""){};

        static const operation_type_enum type;

        address      owner;
        string       domain_name;
        variant      value;

        void evaluate( transaction_evaluation_state& eval_state );
    };

    struct update_auction_operation
    {
        update_auction_operation():domain_name(""),bid(0){};


        static const operation_type_enum   type;

        string                             domain_name;
        address                            bidder;
        asset                              bid;

        void evaluate( transaction_evaluation_state& eval_state );
    };

}}; // bts::blockchain

FC_REFLECT( bts::blockchain::update_domain_operation, (owner)(domain_name)(value) );
FC_REFLECT( bts::blockchain::update_auction_operation, (domain_name)(bidder)(bid) );
