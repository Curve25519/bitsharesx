#include <bts/network/node.hpp>
#include <bts/network/config.hpp>
#include <bts/client/client.hpp>
#include <bts/network/exceptions.hpp>
#include <fc/io/raw.hpp>

namespace bts { namespace network {


   node::node()
   :_desired_peer_count(1)
   {
   }

   node::~node()
   {
      try {
         _server.close();
         auto tmp_copy = _peers;
         for( auto peer : tmp_copy )
         {
            peer->_socket.close();
         }

         if( _accept_loop.valid() )
         {
            _accept_loop.cancel();
            _accept_loop.wait();
         }
      }
      catch ( const fc::exception& e )
      {
         wlog( "${e}", ("e",e.to_detail_string() ) );
      }
   }
   void node::set_client( client_ptr c )
   {
      _client = c;
      fc::schedule( [=](){ attempt_new_connection(); }, fc::time_point::now() + fc::seconds(1) );
   }

   void node::listen( const fc::ip::endpoint& ep )
   { try {
       ulog( "delegate node listening on ${ep}", ("ep",ep) );
      _server.listen( ep );
      _accept_loop = fc::async( [this](){ accept_loop(); } );
   } FC_CAPTURE_AND_RETHROW( (ep) ) }

   vector<peer_info> node::get_peers()const
   {
      vector<peer_info> results;

      for( auto peer : _peers )
      {
         results.push_back( peer->info );
      }

      return results;
   }

   void node::accept_loop()
   {
      while( !_accept_loop.canceled() )
      {
         auto new_connection = std::make_shared<peer_connection>();
         _server.accept( new_connection->_socket );
         on_connect( new_connection );
      }
   }



   void node::on_received_message( const shared_ptr<peer_connection>& con, 
                                   const message& msg )
   {
      try {
         switch ( (message_type)msg.type )
         {
            case message_type::signin:
               on_signin_request( con, msg.as<signin_request>() );
               break;
            case goodbye:
               on_goodbye( con, msg.as<goodbye_message>() );
               break;
            case peer_request:
               break;
            case peer_response:
               break;
            default:
               FC_ASSERT( !"unknown message type", "",("type",msg.type) );
         }
      } 
      catch ( const fc::exception& e )
      {
         wlog( "exception processing message ${m}", ("m",e.to_detail_string() ) );
         con->send_message( goodbye_message(e) ); 
         con->_socket.close();
      }
   } // on_received_message

   void node::on_peer_request( const shared_ptr<peer_connection>& con, 
                               const peer_request_message& request )
   {
      FC_ASSERT( con->is_logged_in(), "not logged in" );
   }

   void node::on_peer_response( const shared_ptr<peer_connection>& con, 
                                const peer_request_message& request )
   {

   }

   void node::on_signin_request( const shared_ptr<peer_connection>& con, 
                                 const signin_request& request )
   { try {
       fc::ecc::public_key  signin_key( request.signature, 
                                        fc::sha256::hash( (char*)&con->_shared_secret, 
                                                          sizeof(con->_shared_secret) ) ); 
       
      auto chain   = _client->get_chain();
      auto account = chain->get_account_record( signin_key );

      FC_ASSERT( account.valid() );

      auto active_delegate_ids = chain->current_round_active_delegates();
      auto location = std::find( active_delegate_ids.begin(), active_delegate_ids.end(), account->id );

      FC_ASSERT( location != active_delegate_ids.end(), 
                 "You must be an active delgate to login" )
      
      con->info.delegate_ids.push_back( signin_key );

   } FC_CAPTURE_AND_RETHROW() }

   void node::on_goodbye( const shared_ptr<peer_connection>& con,
                          const goodbye_message& msg )
   {
      if( msg.reason )
      {
         elog( "Peer ${ep} disconnected us:\n${reason}",
               ("ep",con->_socket.remote_endpoint())("e",msg.reason->to_detail_string() ) );
      }
      con->_socket.close();
   }

   void node::connect_to( const fc::ip::endpoint& remote_peer )
   { try {
      _potential_peers[remote_peer].peer_endpoint = remote_peer;

      for( auto con : _peers )
      {
         if( con->_socket.remote_endpoint() == remote_peer )
            return; // already connected
      }

      auto con = std::make_shared<peer_connection>();
      con->_socket.bind( _server.local_endpoint() );
      con->_socket.connect_to( remote_peer );
      on_connect( con );
   } FC_CAPTURE_AND_RETHROW( (remote_peer) ) }

   void node::on_connect( const shared_ptr<peer_connection>& con )
   { try {
      _peers.insert(con);
      auto remote_ep = con->_socket.remote_endpoint();
      ulog( "node connected ${e}", ("e", remote_ep ) );

      _potential_peers[remote_ep].peer_endpoint = remote_ep;
      _potential_peers[remote_ep].is_connected    = true;

      con->connection_closed.connect( 
              [this]( const shared_ptr<peer_connection>& con, 
                      const optional<fc::exception>& err )
              {
                 on_disconnect( con, err );
              } );
      con->start_read_loop();
   } FC_CAPTURE_AND_RETHROW() }

   void node::on_disconnect( const shared_ptr<peer_connection>& con, 
                             const optional<fc::exception>& err )
   {
      auto remote_ep = con->_socket.remote_endpoint();
      ulog( "node disconnected ${e}", ("e", remote_ep ) );
      _potential_peers[remote_ep].peer_endpoint = remote_ep;
      _potential_peers[remote_ep].is_connected    = false;
      _potential_peers[remote_ep].failed_attempts++;

      if( err )
      {
          wlog( "disconnecting peer ${reason}", ("reason", err->to_detail_string() ) );
      }
      _peers.erase( con );
   }

   void node::signin( const vector<fc::ecc::private_key>& keys )
   { try {
      for( auto key : keys )
      {
         auto itr = std::find( _signin_keys.begin(), _signin_keys.end(), key );
         if( itr == _signin_keys.end() )
         {
            _signin_keys.push_back( key );

            auto peers_copy = _peers;
            // send sign message to all peers
            for( auto peer : peers_copy )
            {
               peer->signin( key );
            }
         }
      }
   } FC_CAPTURE_AND_RETHROW() }

   void node::attempt_new_connection()
   {
      for( auto p : _potential_peers )
      {
        // try a connection...
        if( !p.second.is_connected )
        {
           connect_to( p.second.peer_endpoint );
           p.second.last_connect_attempt = fc::time_point::now();
        }
      }
      if( _peers.size() < _desired_peer_count )
         fc::schedule( [=](){ attempt_new_connection(); }, fc::time_point::now() + fc::seconds(1) );
   }


} } // bts::network
