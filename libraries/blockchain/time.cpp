#include <bts/blockchain/config.hpp>
#include <bts/blockchain/time.hpp>

#include <fc/exception/exception.hpp>
#include <fc/network/ntp.hpp>

#include <atomic>

namespace bts { namespace blockchain {

static int32_t simulated_time    = 0;
static int32_t adjusted_time_sec = 0;

time_discontinuity_signal_type time_discontinuity_signal;

namespace detail
{
  std::atomic<fc::ntp*> ntp_service(nullptr);
}

fc::optional<fc::time_point> ntp_time()
{
  fc::ntp* actual_ntp_service = detail::ntp_service.load();
  if (!actual_ntp_service)
  {
    actual_ntp_service = new fc::ntp;
    fc::ntp* old_ntp_service = nullptr;
    bool exchanged = detail::ntp_service.compare_exchange_strong(old_ntp_service, actual_ntp_service);
    if (!exchanged)
    {
      delete actual_ntp_service;
      actual_ntp_service = old_ntp_service;
    }
  }
  return actual_ntp_service->get_time();
}

void shutdown_ntp_time()
{
  fc::ntp* actual_ntp_service = detail::ntp_service.exchange(nullptr);
  delete actual_ntp_service;
}

fc::time_point_sec now()
{
   if( simulated_time )
       return fc::time_point() + fc::seconds( simulated_time + adjusted_time_sec );

   fc::optional<fc::time_point> current_ntp_time = ntp_time();
   if( current_ntp_time.valid() )
      return *current_ntp_time + fc::seconds( adjusted_time_sec );
   else
      return fc::time_point::now() + fc::seconds( adjusted_time_sec );
}

fc::microseconds ntp_error()
{
  fc::optional<fc::time_point> current_ntp_time = ntp_time();
  FC_ASSERT( current_ntp_time, "We don't have NTP time!" );
  return *current_ntp_time - fc::time_point::now();
}

void start_simulated_time( const fc::time_point& sim_time )
{
   simulated_time = sim_time.sec_since_epoch();
   adjusted_time_sec = 0;
}

void advance_time( int32_t delta_seconds )
{
   adjusted_time_sec += delta_seconds;
   time_discontinuity_signal();
}

uint32_t get_slot_number( const fc::time_point_sec& timestamp )
{
   return timestamp.sec_since_epoch() / BTS_BLOCKCHAIN_BLOCK_INTERVAL_SEC;
}

fc::time_point_sec get_slot_start_time( uint32_t slot_number )
{
   return fc::time_point_sec( slot_number * BTS_BLOCKCHAIN_BLOCK_INTERVAL_SEC );
}

fc::time_point_sec get_slot_start_time( const fc::time_point_sec& timestamp )
{
   return get_slot_start_time( get_slot_number( timestamp ) );
}

} } // bts::blockchain
