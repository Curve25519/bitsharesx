#include <bts/blockchain/dns_operations.hpp>
#include <bts/blockchain/chain_interface.hpp>
#include <bts/blockchain/exceptions.hpp>
#include <bts/blockchain/transaction_evaluation_state.hpp>
#include <bts/blockchain/dns_utils.hpp>
#include <bts/blockchain/dns_config.hpp>

namespace bts { namespace blockchain { 

    void update_domain_operation::evaluate( transaction_evaluation_state& eval_state )
    {
        FC_ASSERT( is_valid_domain( this->domain_name ) );
        auto now = eval_state._current_state->now().sec_since_epoch();
        auto odomain_rec = eval_state._current_state->get_domain_record( this->domain_name );
        /* First bid case */
        if (this->update_type == domain_record::first_bid)
        {

            FC_ASSERT(NOT odomain_rec.valid() || now > odomain_rec->last_update + P2P_EXPIRE_DURATION_SECS,
              "Attempted to start auction for a domain that is not expired or is in auction" );

            FC_ASSERT(this->bid_amount >= P2P_MIN_INITIAL_BID, "Not large enough initial bid.");
            FC_ASSERT(this->value.as_string() == variant("").as_string());
            eval_state.required_fees += asset(this->bid_amount);
            
            domain_record updated_record = domain_record(); 
            updated_record.domain_name = this->domain_name;
            updated_record.owner = this->owner;
            updated_record.value = this->value;
            updated_record.last_update = now;
            updated_record.update_type = domain_record::first_bid;
            updated_record.last_bid = this->bid_amount;
            updated_record.next_required_bid = P2P_NEXT_REQ_BID(0, this->bid_amount);
            
            eval_state._current_state->store_domain_record( updated_record );
        }
        /* Normal bid case */
        else if( this->update_time == domain_record::bid )
        {
            FC_ASSERT(odomain_rec.valid() && now < odomain_rec->last_update + P2P_AUCTION_DURATION_SECS,
                     "Attempting to make a normal bid on a domain that is not in auction.");
            FC_ASSERT(this->bid_amount >= odomain_rec->next_required_bid,
                     "Bid is not high enough.");

            auto bid_difference = this->bid_amount - odomain_rec->bid_amount;
            // TODO require payment back to owner
            eval_state.required_fees += asset(bid_difference * P2P_DIVIDEND_RATIO);

            FC_ASSERT(this->value.as_string() == variant("").as_string());
            
            domain_record updated_record = domain_record(); 
            updated_record.domain_name = this->domain_name;
            updated_record.owner = this->owner;
            updated_record.value = this->value;
            updated_record.last_update = now;
            updated_record.update_type = domain_record::bid;
            updated_record.last_bid = this->bid_amount;
            updated_record.next_required_bid = P2P_NEXT_REQ_BID(odomain_rec->bid_amount, this->bid_amount);
            
            eval_state._current_state->store_domain_record( updated_record ); 
        }
    }

}} // bts::blockchain
