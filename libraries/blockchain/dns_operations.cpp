#include <bts/blockchain/dns_operations.hpp>
#include <bts/blockchain/chain_interface.hpp>
#include <bts/blockchain/exceptions.hpp>
#include <bts/blockchain/transaction_evaluation_state.hpp>


namespace bts { namespace blockchain { 

    void update_domain_operation::evaluate( transaction_evaluation_state& eval_state )
    {

    }


    void update_auction_operation::evaluate( transaction_evaluation_state& eval_state )
    {
        // no other dns update operations
        // name is valid
        // address is valid address
        // bid_amount is the right asset

        // if expired or never in auction, anyone can make initial bid
        //     * outputs-inputs higher than minimum bid
        //     * signed by bidder
    }


}} // bts::blockchain
