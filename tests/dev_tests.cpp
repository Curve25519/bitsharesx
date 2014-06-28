#define BOOST_TEST_MODULE BlockchainTests2cc
#include <boost/test/unit_test.hpp>
#include "dev_fixture.hpp"

BOOST_AUTO_TEST_CASE( timetest )
{ 
  auto block_time =  fc::variant( "20140617T024645" ).as<fc::time_point_sec>();
  auto now =  fc::variant( "20140617T024332" ).as<fc::time_point_sec>();
  elog( "delta: ${d}", ("d", (block_time - now).to_seconds() ) );
}

BOOST_FIXTURE_TEST_CASE( basic_commands, chain_fixture )
{ try {
//   disable_logging();
   enable_logging();
   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "wallet_list_my_accounts" );
   exec( clienta, "wallet_account_balance" );
   exec( clienta, "unlock 999999999 masterpassword" );
   exec( clienta, "scan 0 100" );
   exec( clienta, "wallet_account_balance" );
   exec( clienta, "close" );
   exec( clienta, "open walleta" );
   exec( clienta, "unlock 99999999 masterpassword" );
   exec( clienta, "wallet_account_balance" );
   exec( clienta, "wallet_account_balance delegate31" );
   exec( clienta, "wallet_enable_delegate_block_production delegate31 true" );
   exec( clienta, "wallet_enable_delegate_block_production delegate33 true" );
   exec( clienta, "wallet_set_delegate_trust delegate33 true" );
   exec( clienta, "wallet_set_delegate_trust delegate34 true" );
   exec( clienta, "wallet_set_delegate_trust delegate35 true" );
   exec( clienta, "wallet_set_delegate_trust delegate36 true" );
   exec( clienta, "wallet_set_delegate_trust delegate37 true" );
   exec( clienta, "wallet_set_delegate_trust delegate38 true" );
   exec( clienta, "wallet_set_delegate_trust delegate39 true" );

   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "info" );
   exec( clientb, "wallet_set_delegate_trust delegate23 true" );
   exec( clientb, "wallet_set_delegate_trust delegate24 true" );
   exec( clientb, "wallet_set_delegate_trust delegate25 true" );
   exec( clientb, "wallet_set_delegate_trust delegate26 true" );
   exec( clientb, "wallet_set_delegate_trust delegate27 true" );
   exec( clientb, "wallet_set_delegate_trust delegate28 true" );
   exec( clientb, "wallet_set_delegate_trust delegate29 true" );

   exec( clientb, "wallet_list_my_accounts" );
   exec( clientb, "wallet_account_balance" );
   exec( clientb, "wallet_account_balance delegate30" );
   exec( clientb, "unlock 999999999 masterpassword" );
   exec( clientb, "wallet_enable_delegate_block_production delegate30 true" );
   exec( clientb, "wallet_enable_delegate_block_production delegate32 true" );

   exec( clientb, "wallet_account_create b-account" );
   exec( clientb, "wallet_account_balance b-account" );

   exec( clientb, "wallet_account_register b-account delegate30 null 100" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   produce_block( clienta );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "wallet_list_my_accounts" );
   exec( clientb, "wallet_account_update_registration b-account delegate30 { \"ip\":\"localhost\"} true" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   produce_block( clienta );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "wallet_list_my_accounts" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "wallet_transfer 33 XTS delegate31 b-account first-memo" );
   exec( clienta, "wallet_account_transaction_history delegate31" );
   exec( clienta, "wallet_account_transaction_history b-account" );
   exec( clienta, "wallet_account_transaction_history" );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "wallet_account_transaction_history b-account" );
   produce_block( clientb );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "wallet_account_transaction_history delegate31" );
   exec( clienta, "wallet_account_transaction_history b-account" );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "wallet_account_transaction_history b-account" );
   exec( clientb, "wallet_create_account c-account" );
   exec( clientb, "wallet_transfer 10 XTS b-account c-account to-me" );
   exec( clientb, "wallet_account_transaction_history b-account" );
   exec( clientb, "wallet_account_transaction_history c-account" );
   produce_block( clientb );
   exec( clientb, "wallet_account_transaction_history c-account" );
   exec( clientb, "blockchain_list_delegates" );
   exec( clientb, "wallet_set_delegate_trust b-account true" );
   exec( clientb, "wallet_list_my_accounts" );
   exec( clientb, "balance" );
   exec( clientb, "wallet_transfer 100000 XTS delegate32 c-account to-me" );
   exec( clientb, "wallet_transfer 100000 XTS delegate30 c-account to-me" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "wallet_set_delegate_trust b-account true" );
   // TODO: this should throw an exception from the wallet regarding delegate_vote_limit, but it produces
   // the transaction anyway.   
   // TODO: before fixing the wallet production side to include multiple outputs and spread the vote, 
   // the transaction history needs to show the transaction as an 'error' rather than 'pending' and
   // properly display the reason for the user.
   // TODO: provide a way to cancel transactions that are pending.
   exec( clienta, "wallet_transfer 100000 XTS delegate31 b-account to-b" );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   produce_block( clientb );
   exec( clientb, "balance" );
   exec( clientb, "wallet_account_transaction_history c-account" );
   exec( clientb, "blockchain_list_delegates" );
   exec( clientb, "wallet_asset_create USD Dollar b-account \"paper bucks\" null 1000000000 1000" );
   produce_block( clientb );
   exec( clientb, "blockchain_list_registered_assets" );
   exec( clientb, "wallet_asset_issue 1000 USD c-account \"iou\"" );
   exec( clientb, "wallet_account_transaction_history b-account" );
   exec( clientb, "wallet_account_transaction_history c-account" );
   produce_block( clientb );
   exec( clientb, "wallet_account_transaction_history b-account" );
   exec( clientb, "wallet_account_transaction_history c-account" );
   exec( clientb, "wallet_transfer 20 USD c-account delegate31 c-d31" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   produce_block( clienta );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "wallet_account_transaction_history c-account" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "wallet_account_transaction_history delegate31" );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "balance" );
   exec( clientb, "bid c-account 120 XTS 4.50 USD" );
   exec( clientb, "bid c-account 40 XTS 2.50 USD" );
   produce_block( clientb );
   exec( clientb, "wallet_account_transaction_history c-account" );
   exec( clientb, "balance" );
   exec( clientb, "blockchain_market_list_bids USD XTS" );
   exec( clientb, "wallet_market_order_list USD XTS" );
   auto result = clientb->wallet_market_order_list( "USD", "XTS" );
   exec( clientb, "wallet_market_cancel_order " + string( result[0].order.market_index.owner ) );
   produce_block( clientb );
   exec( clientb, "wallet_market_order_list USD XTS" );
   exec( clientb, "blockchain_market_list_bids USD XTS" );
   exec( clientb, "wallet_account_transaction_history" );
   exec( clientb, "balance" );

   result = clientb->wallet_market_order_list( "USD", "XTS" );
   exec( clientb, "wallet_market_cancel_order " + string( result[0].order.market_index.owner ) );
   produce_block( clientb );
   exec( clientb, "blockchain_market_list_bids USD XTS" );
   exec( clientb, "wallet_account_transaction_history" );
   exec( clientb, "balance" );
   exec( clientb, "wallet_change_passphrase newmasterpassword" );
   exec( clientb, "close" );
   exec( clientb, "open walletb" );
   exec( clientb, "unlock 99999999 newmasterpassword" );
   exec( clientb, "blockchain_get_transaction d387d39ca1" );

   exec( clientb, "wallet_transfer 20 USD c-account delegate31 c-d31" );
   exec( clientb, "blockchain_get_pending_transactions" );
   enable_logging();
   exec( clientb, "wallet_market_order_list USD XTS" );
   exec( clientb, "wallet_account_transaction_history" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   produce_block( clienta );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   disable_logging();
   for( uint32_t i = 0; i < 100; ++i )
   {
//      exec( clientb, "wallet_transfer 10 XTS delegate32 delegate32 " );
      produce_block( clientb );
   }
   exec( clientb, "blockchain_get_account_record delegate32" );
   exec( clientb, "wallet_withdraw_delegate_pay delegate32 c-account .01234" );
   produce_block( clientb );
   exec( clientb, "wallet_account_transaction_history delegate32" );
   exec( clientb, "blockchain_list_delegates" );


   exec( clienta, "wallet_set_delegate_trust delegate33 false" );
   exec( clienta, "wallet_set_delegate_trust delegate34 false" );
   exec( clienta, "wallet_set_delegate_trust delegate35 false" );
   exec( clienta, "wallet_set_delegate_trust delegate36 false" );
   exec( clienta, "wallet_set_delegate_trust delegate37 false" );
   exec( clienta, "wallet_set_delegate_trust delegate38 false" );
   exec( clienta, "wallet_set_delegate_trust delegate39 false" );


   exec( clientb, "wallet_set_delegate_trust delegate23 false" );
   exec( clientb, "wallet_set_delegate_trust delegate24 false" );
   exec( clientb, "wallet_set_delegate_trust delegate25 false" );
   exec( clientb, "wallet_set_delegate_trust delegate26 false" );
   exec( clientb, "wallet_set_delegate_trust delegate27 false" );
   exec( clientb, "wallet_set_delegate_trust delegate28 false" );
   exec( clientb, "wallet_set_delegate_trust delegate29 false" );

   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "wallet_set_delegate_trust delegate44 true" );
   exec( clienta, "wallet_set_delegate_trust delegate44 true" );
   exec( clienta, "wallet_set_delegate_trust delegate45 true" );
   exec( clienta, "wallet_set_delegate_trust delegate46 true" );
   exec( clienta, "wallet_set_delegate_trust delegate47 true" );
   exec( clienta, "wallet_set_delegate_trust delegate48 true" );
   exec( clienta, "wallet_set_delegate_trust delegate49 true" );

   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "wallet_set_delegate_trust delegate63 true" );
   exec( clientb, "wallet_set_delegate_trust delegate64 true" );
   exec( clientb, "wallet_set_delegate_trust delegate65 true" );
   exec( clientb, "wallet_set_delegate_trust delegate66 true" );
   exec( clientb, "wallet_set_delegate_trust delegate67 true" );
   exec( clientb, "wallet_set_delegate_trust delegate68 true" );
   exec( clientb, "wallet_set_delegate_trust delegate69 true" );
   exec( clientb, "balance" );
   exec( clienta, "balance" );
   exec( clienta, "wallet_transfer 10691976.59801 XTS delegate31 delegate31 change_votes " );
   exec( clienta, "wallet_transfer 10801980.09801  XTS delegate33 delegate33 change_votes " );
   exec( clientb, "wallet_transfer 9792.18499 XTS b-account b-account change_votes " );
   exec( clientb, "wallet_transfer 20000.40123 XTS c-account c-account change_votes " );
   exec( clientb, "wallet_transfer 10791970.09801 XTS delegate32 delegate32 change_votes " );
   exec( clientb, "wallet_transfer 10791760.18284 XTS delegate30 delegate30 change_votes " );

   wlog( "------------------  CLIENT A  -----------------------------------" );
   produce_block( clienta );
   exec( clienta, "balance" );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "blockchain_list_delegates" );

   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "wallet_transfer 10691976.59801 XTS delegate31 delegate31 change_votes " );
   exec( clienta, "wallet_transfer 10801980.09801  XTS delegate33 delegate33 change_votes " );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "wallet_transfer 9792.18499 XTS b-account b-account change_votes " );
   exec( clientb, "wallet_transfer 20000.40123 XTS c-account c-account change_votes " );
   exec( clientb, "wallet_transfer 10791970.09801 XTS delegate32 delegate32 change_votes " );
   exec( clientb, "wallet_transfer 10791760.18284 XTS delegate30 delegate30 change_votes " );

   exec( clientb, "info" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "info" );

   enable_logging();
   wlog( "FORKING NETWORKS" );
   clientb->simulate_disconnect(true);
   produce_block(clienta);
   produce_block(clienta);
   produce_block(clienta);
   produce_block(clienta);
   produce_block(clienta);
   wlog( "------------------  CLIENT B  -----------------------------------" );
   clientb->simulate_disconnect(false);

   wlog( "------------------  CLIENT A  -----------------------------------" );
   clienta->simulate_disconnect(true);
   wlog( "------------------  CLIENT B  -----------------------------------" );
   produce_block(clientb);
   produce_block(clientb);
   produce_block(clientb);

   wlog( "------------------  CLIENT A  -----------------------------------" );
   clienta->simulate_disconnect(false);
   produce_block(clienta);
   produce_block(clienta);
   produce_block(clienta);

   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "info" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "info" );

   wlog( "JOINING NETWORKS" );
   for( uint32_t i = 2; i <= clienta->get_chain()->get_head_block_num(); ++i )
   {
      auto b = clienta->get_chain()->get_block( i );
      clientb->get_chain()->push_block(b);
   }

   wlog( "------------------  CLIENT B  -----------------------------------" );
   exec( clientb, "info" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   exec( clienta, "info" );

//   exec( clientb, "blockchain_get_transaction 6f28bd041522ebf968009b1ff85dcc6355d80cb7" );


//   exec( clientb, "wallet_account_transaction_history" );
//   exec( clientb, "blockchain_get_transaction 6f28bd04" );
//   exec( clientb, "blockchain_list_current_round_active_delegates" );
//   exec( clientb, "blockchain_list_blocks" );



} FC_LOG_AND_RETHROW() }




