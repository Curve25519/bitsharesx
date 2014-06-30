#pragma once

#include <bts/blockchain/account_record.hpp>
#include <bts/blockchain/block_record.hpp>
#include <bts/blockchain/types.hpp>
#include <bts/client/client.hpp>
#include <bts/wallet/pretty.hpp>
#include <bts/wallet/wallet.hpp>

#include <fc/time.hpp>

#include <string>

namespace bts { namespace cli {

using namespace bts::blockchain;
using namespace bts::wallet;

typedef bts::client::client const * const cptr;

string pretty_line( int size );
string pretty_shorten( const string& str, size_t max_size );
string pretty_timestamp( const time_point_sec& timestamp );
string pretty_percent( double part, double whole, int precision = 2 );

string pretty_delegate_list( const vector<account_record>& delegate_records, cptr client );

string pretty_block_list( const vector<block_record>& block_records, cptr client );

string pretty_transaction_list( const vector<pretty_transaction>& transactions, cptr client );

string pretty_vote_summary( const account_vote_summary_type& votes );

} } // bts::cli
