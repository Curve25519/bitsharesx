#include <bts/blockchain/types.hpp>
#include <bts/blockchain/dns_config.hpp>
#include <bts/blockchain/dns_utils.hpp>
#include <fc/time.hpp>

namespace bts { namespace blockchain {

    bool is_valid_domain( const std::string& str )
    {
        if( str.size() < P2P_MIN_DOMAIN_NAME_SIZE ) return false;
        if( str.size() > P2P_MAX_DOMAIN_NAME_SIZE ) return false;
        if( str[0] < 'a' || str[0] > 'z' ) return false;
        for( const auto& c : str )
        {
            if( c >= 'a' && c <= 'z' ) continue;
            else if( c >= '0' && c <= '9' ) continue;
            else if( c == '-' ) continue;
            // else if( c == '.' ) continue;   TODO subdomain logic
            else return false;
        }
        return true;
    }

    bool is_auction_over( const auction_record& rec )
    {
        return true;
    }

    bool is_domain_expired( const domain_record& rec )
    {
        return true;
    }

    bool can_start_auction( const oauction_record& oauction_rec, const odomain_record& odomain_rec )
    {
        /* You can start an auction if there has never been an auction for this name before,
         * OR if the last domain update or last auction updates, whichever happened LATER, is over
         * EXPIRE_DURATION_SECS ago
         */
        int now = fc::time_point::now().sec_since_epoch();

        if ( !oauction_rec.valid() )
            return true;
        if ( !odomain_rec.valid() )
        {
            if ( (now - oauction_rec->last_bid_time)
                > (P2P_EXPIRE_DURATION_SECS + P2P_AUCTION_DURATION_SECS) )
                return true;
            return false;
        }
        // kill me
        if ( ((now - oauction_rec->last_bid_time)
                > (P2P_EXPIRE_DURATION_SECS + P2P_AUCTION_DURATION_SECS))
             && (now - odomain_rec->last_update)
                > (P2P_EXPIRE_DURATION_SECS) )
            return true;
        return false;
    }

}} // bts::blockchain
