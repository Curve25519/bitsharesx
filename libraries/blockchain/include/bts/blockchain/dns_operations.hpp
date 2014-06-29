#pragma once
#include <bts/blockchain/operations.hpp>
#include <bts/blockchain/types.hpp>
#include <bts/blockchain/dns_record.hpp>
#include <fc/io/enum_type.hpp>

namespace bts { namespace blockchain {

    struct update_domain_operation
    {
        update_domain_operation():domain_name(""){}

        static const operation_type_enum type;

        address                  owner;
        string                   domain_name;
        variant                  value;
        fc::enum_type<uint8_t, domain_record::domain_update_type>       last_update_type;
        share_type               bid;
        share_type               next_required_bid;

        void evaluate( transaction_evaluation_state& eval_state );
    };

}}; // bts::blockchain

FC_REFLECT( bts::blockchain::update_domain_operation, (owner)(domain_name)(value)(bid)(next_required_bid)(last_update_type) );
