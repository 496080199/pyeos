/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <currency/currency.hpp>

namespace exchange {

   using currency::currency_tokens;
   using eos_tokens = eos::tokens;

   struct order_id {
      account_name name    = 0;
      uint64_t    number  = 0;
   };

   typedef eos::price<eos_tokens,currency_tokens>     price;

   struct PACKED( bid ) {
      order_id           buyer;
      price              at_price;
      eos::tokens        quantity;
      time               expiration;

      void print() {
         eos::print( "{ quantity: ", quantity, ", price: ", at_price, " }" );
      }
   };
   static_assert( sizeof(bid) == 32+12, "unexpected padding" );

   struct PACKED( ask ) {
      order_id         seller;
      price            at_price;
      currency_tokens  quantity;
      time             expiration;

      void print() {
         eos::print( "{ quantity: ", quantity, ", price: ", at_price, " }" );
      }
   };
   static_assert( sizeof(ask) == 32+12, "unexpected padding" );

   struct PACKED( account ) {
      account( account_name o = account_name() ):owner(o){}

      account_name       owner;
      eos_tokens         eos_balance;
      currency_tokens    currency_balance;
      uint32_t           open_orders = 0;

      bool is_empty()const { return ! ( bool(eos_balance) | bool(currency_balance) | open_orders); }
   };

   using accounts = table<N(exchange),N(exchange),N(account),account,uint64_t>;

   TABLE2(bids,exchange,exchange,bids,bid,bids_by_id,order_id,bids_by_price,price);
   TABLE2(asks,exchange,exchange,asks,ask,asks_by_id,order_id,asks_by_price,price);


   struct buy_order : public bid  { uint8_t fill_or_kill = false; };
   struct sell_order : public ask { uint8_t fill_or_kill = false; };


   inline account get_account( account_name owner ) {
      account owned_account(owner);
      accounts::get( owned_account );
      return owned_account;
   }
}

