#pragma once
#include <fc/io/raw.hpp>
#include <bts/blockchain/block.hpp>
#include <bts/network/peer_info.hpp>
#include <fc/exception/exception.hpp>
#include <fc/io/raw_variant.hpp>
#include <vector>

namespace bts { namespace network {
   
   struct message
   {
      uint32_t            size;
      uint8_t             type;
      std::vector<char>   data;

      message():size(0),type(0){}

      message( const message& copy )
      :size(copy.size),type(copy.type),data(copy.data){}

      /**
       *  Assumes that T::type specifies the message type
       */
      template<typename T>
      message( const T& m ) 
      {
         type = T::type;
         data = fc::raw::pack(m);
         size = data.size();
      }

      template<typename T>
      T as()const
      {
         FC_ASSERT( T::type == type );

         T tmp;
         if( size )
         {
            FC_ASSERT( size <= data.size() );
            fc::datastream<const char*> ds( data.data(), size );
            fc::raw::unpack( ds, tmp );
         }
         else
         {
            // just to make sure that tmp shouldn't have any data
            fc::datastream<const char*> ds( nullptr, 0 );
            fc::raw::unpack( ds, tmp );
         }
         return tmp;
      }
   }; 
   enum message_type
   {
      keep_alive      =  0,
      signin          =  1,
      goodbye         = -1,
      peer_request    =  2,
      peer_response   = -2,
      block           =  3
   };

   struct signin_request
   {
      enum type_enum { type = signin };
      signin_request(){}

      signin_request( const fc::ecc::compact_signature& s )
      :signature(s){}

      // signature on the shared secret
      fc::ecc::compact_signature signature;
   };

   struct goodbye_message
   {
      enum type_enum { type = goodbye };

      goodbye_message(){};
      goodbye_message( const fc::exception& e )
      :reason(e){}

      fc::optional<fc::exception> reason;
   };

   struct peer_request_message
   {
      enum type_enum { type = peer_request };
   };

   struct  peer_response_message
   {
      enum type_enum { type = peer_response };
      vector<peer_info>  peers;
   };

   struct block_message
   {
      enum type_enum { type = message_type::block };

      block_message(){}
      block_message( const full_block& b )
      :block(b){}

      full_block block;
   };

   struct keep_alive_message
   {
      enum type_enum { type = keep_alive };
      keep_alive_message(){}
      keep_alive_message( const fc::time_point& n ):time(n){}

      fc::time_point  time;
   };


} } // bts::network

FC_REFLECT_ENUM( bts::network::message_type,
                 (keep_alive)
                 (signin)
                 (goodbye)
                 (peer_request)
                 (peer_response) 
                 (block)
                 )

FC_REFLECT( bts::network::signin_request, (signature) )
FC_REFLECT( bts::network::goodbye_message, (reason) )
FC_REFLECT( bts::network::peer_response_message, (peers) )
FC_REFLECT( bts::network::block_message, (block) )
FC_REFLECT( bts::network::keep_alive_message, (time) );
