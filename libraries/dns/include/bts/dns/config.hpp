#pragma once

#define P2P_MIN_INIITIAL_BID (10)
#define P2P_MIN_BID_INCREASE_RATIO (101.0 / 100.0)
#define P2P_REQUIRED_BID_DIFF_RATIO (1)  // my_required = prev_bid + R*(prev_bid - prev_required)
#define P2P_DIVIDEND_RATIO (0.5)  //
#define P2P_KICKBACK_RATIO (1 - P2P_DIVIDEND_RATIO)

#define P2P_MAX_CONCURRENT_AUCTIONS_FOR_BLOCK(block) (50)
#define P2P_AUCTION_DURATION_SECS (60*60*24  * 3)
#define P2P_EXPIRE_DURATION_SECS (60*60*24  * 365)

#define P2P_MIN_DOMAIN_NAME_SIZE (1)
#define P2P_MAX_DOMAIN_NAME_SIZE (63)

