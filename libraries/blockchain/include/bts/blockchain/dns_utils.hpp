#pragma once
#include <bts/blockchain/types.hpp>
#include <bts/blockchain/dns_record.hpp>

namespace bts { namespace blockchain {

    bool is_valid_domain( const std::string& domain_name );
    bool is_auction_over( const auction_record& rec );
    bool is_domain_expired( const domain_record& rec );
    bool can_start_auction( const oauction_record& oauction_rec, const odomain_record& odomain_rec );

}}
