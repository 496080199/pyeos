/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once
#include <eosio/chain/block_trace.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/contracts/contract_table_objects.hpp>
#include <eosio/chain/webassembly/common.hpp>

#include <fc/utility.hpp>
#include <sstream>
#include <algorithm>
#include <set>

namespace chainbase { class database; }

namespace eosio { namespace chain {

using contracts::key_value_object;

class chain_controller;

class database_api {
   private:
      template<typename T>
      class iterator_cache {
         public:
            typedef contracts::table_id_object table_id_object;

            iterator_cache(){
               _end_iterator_to_table.reserve(8);
               _iterator_to_object.reserve(32);
            }

            /// Returns end iterator of the table.
            int cache_table( const table_id_object& tobj ) {
               auto itr = _table_cache.find(tobj.id);
               if( itr != _table_cache.end() )
                  return itr->second.second;

               auto ei = index_to_end_iterator(_end_iterator_to_table.size());
               _end_iterator_to_table.push_back( &tobj );
               _table_cache.emplace( tobj.id, make_pair(&tobj, ei) );
               return ei;
            }

            const table_id_object& get_table( table_id_object::id_type i )const {
               auto itr = _table_cache.find(i);
               FC_ASSERT( itr != _table_cache.end(), "an invariant was broken, table should be in cache" );
               return *itr->second.first;
            }

            int get_end_iterator_by_table_id( table_id_object::id_type i )const {
               auto itr = _table_cache.find(i);
               FC_ASSERT( itr != _table_cache.end(), "an invariant was broken, table should be in cache" );
               return itr->second.second;
            }

            const table_id_object* find_table_by_end_iterator( int ei )const {
               FC_ASSERT( ei < -1, "not an end iterator" );
               auto indx = end_iterator_to_index(ei);
               if( indx >= _end_iterator_to_table.size() ) return nullptr;
               return _end_iterator_to_table[indx];
            }

            const T& get( int iterator ) {
               FC_ASSERT( iterator != -1, "invalid iterator" );
               FC_ASSERT( iterator >= 0, "dereference of end iterator" );
               FC_ASSERT( iterator < _iterator_to_object.size(), "iterator out of range" );
               auto result = _iterator_to_object[iterator];
               FC_ASSERT( result, "dereference of deleted object" );
               return *result;
            }

            void remove( int iterator ) {
               FC_ASSERT( iterator != -1, "invalid iterator" );
               FC_ASSERT( iterator >= 0, "cannot call remove on end iterators" );
               FC_ASSERT( iterator < _iterator_to_object.size(), "iterator out of range" );
               auto obj_ptr = _iterator_to_object[iterator];
               if( !obj_ptr ) return;
               _iterator_to_object[iterator] = nullptr;
               _object_to_iterator.erase( obj_ptr );
            }

            int add( const T& obj ) {
               auto itr = _object_to_iterator.find( &obj );
               if( itr != _object_to_iterator.end() )
                    return itr->second;

               _iterator_to_object.push_back( &obj );
               _object_to_iterator[&obj] = _iterator_to_object.size() - 1;

               return _iterator_to_object.size() - 1;
            }

         private:
            map<table_id_object::id_type, pair<const table_id_object*, int>> _table_cache;
            vector<const table_id_object*>                  _end_iterator_to_table;
            vector<const T*>                                _iterator_to_object;
            map<const T*,int>                               _object_to_iterator;

            /// Precondition: std::numeric_limits<int>::min() < ei < -1
            /// Iterator of -1 is reserved for invalid iterators (i.e. when the appropriate table has not yet been created).
            inline size_t end_iterator_to_index( int ei )const { return (-ei - 2); }
            /// Precondition: indx < _end_iterator_to_table.size() <= std::numeric_limits<int>::max()
            inline int index_to_end_iterator( size_t indx )const { return -(indx + 2); }
      };

      template<typename>
      struct array_size;

      template<typename T, size_t N>
      struct array_size< std::array<T,N> > {
          static constexpr size_t size = N;
      };

      template <typename SecondaryKey, typename SecondaryKeyProxy, typename SecondaryKeyProxyConst, typename Enable = void>
      class secondary_key_helper;

      template<typename SecondaryKey, typename SecondaryKeyProxy, typename SecondaryKeyProxyConst>
      class secondary_key_helper<SecondaryKey, SecondaryKeyProxy, SecondaryKeyProxyConst,
         typename std::enable_if<std::is_same<SecondaryKey, typename std::decay<SecondaryKeyProxy>::type>::value>::type >
      {
         public:
            typedef SecondaryKey secondary_key_type;

            static void set(secondary_key_type& sk_in_table, const secondary_key_type& sk_from_wasm) {
               sk_in_table = sk_from_wasm;
            }

            static void get(secondary_key_type& sk_from_wasm, const secondary_key_type& sk_in_table ) {
               sk_from_wasm = sk_in_table;
            }

            static auto create_tuple(const contracts::table_id_object& tab, const secondary_key_type& secondary) {
               return boost::make_tuple( tab.id, secondary );
            }
      };

      template<typename SecondaryKey, typename SecondaryKeyProxy, typename SecondaryKeyProxyConst>
      class secondary_key_helper<SecondaryKey, SecondaryKeyProxy, SecondaryKeyProxyConst,
         typename std::enable_if<!std::is_same<SecondaryKey, typename std::decay<SecondaryKeyProxy>::type>::value &&
                                 std::is_pointer<typename std::decay<SecondaryKeyProxy>::type>::value>::type >
      {
         public:
            typedef SecondaryKey      secondary_key_type;
            typedef SecondaryKeyProxy secondary_key_proxy_type;
            typedef SecondaryKeyProxyConst secondary_key_proxy_const_type;

            static constexpr size_t N = array_size<SecondaryKey>::size;

            static void set(secondary_key_type& sk_in_table, secondary_key_proxy_const_type sk_from_wasm) {
               std::copy(sk_from_wasm, sk_from_wasm + N, sk_in_table.begin());
            }

            static void get(secondary_key_proxy_type sk_from_wasm, const secondary_key_type& sk_in_table) {
               std::copy(sk_in_table.begin(), sk_in_table.end(), sk_from_wasm);
            }

            static auto create_tuple(const contracts::table_id_object& tab, secondary_key_proxy_const_type sk_from_wasm) {
               secondary_key_type secondary;
               std::copy(sk_from_wasm, sk_from_wasm + N, secondary.begin());
               return boost::make_tuple( tab.id, secondary );
            }
      };

   public:
      template<typename ObjectType,
               typename SecondaryKeyProxy = typename std::add_lvalue_reference<typename ObjectType::secondary_key_type>::type,
               typename SecondaryKeyProxyConst = typename std::add_lvalue_reference<
                                                   typename std::add_const<typename ObjectType::secondary_key_type>::type>::type >
      class generic_index
      {
         public:
            typedef typename ObjectType::secondary_key_type secondary_key_type;
            typedef SecondaryKeyProxy      secondary_key_proxy_type;
            typedef SecondaryKeyProxyConst secondary_key_proxy_const_type;

            using secondary_key_helper_t = secondary_key_helper<secondary_key_type, secondary_key_proxy_type, secondary_key_proxy_const_type>;

            generic_index( database_api& c ):context(c){}

            int find_secondary( uint64_t code, uint64_t scope, uint64_t table, secondary_key_proxy_const_type secondary, uint64_t& primary ) {
               auto tab = context.find_table( code, scope, table );
               if( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto* obj = context.db.find<ObjectType, contracts::by_secondary>( secondary_key_helper_t::create_tuple( *tab, secondary ) );
               if( !obj ) return table_end_itr;

               primary = obj->primary_key;

               return itr_cache.add( *obj );
            }

            int lowerbound_secondary( uint64_t code, uint64_t scope, uint64_t table, secondary_key_proxy_type secondary, uint64_t& primary ) {
               auto tab = context.find_table( code, scope, table );
               if( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto& idx = context.db.get_index< typename chainbase::get_index_type<ObjectType>::type, contracts::by_secondary >();
               auto itr = idx.lower_bound( secondary_key_helper_t::create_tuple( *tab, secondary ) );
               if( itr == idx.end() ) return table_end_itr;
               if( itr->t_id != tab->id ) return table_end_itr;

               primary = itr->primary_key;
               secondary_key_helper_t::get(secondary, itr->secondary_key);

               return itr_cache.add( *itr );
            }

            int upperbound_secondary( uint64_t code, uint64_t scope, uint64_t table, secondary_key_proxy_type secondary, uint64_t& primary ) {
               auto tab = context.find_table( code, scope, table );
               if( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto& idx = context.db.get_index< typename chainbase::get_index_type<ObjectType>::type, contracts::by_secondary >();
               auto itr = idx.upper_bound( secondary_key_helper_t::create_tuple( *tab, secondary ) );
               if( itr == idx.end() ) return table_end_itr;
               if( itr->t_id != tab->id ) return table_end_itr;

               primary = itr->primary_key;
               secondary_key_helper_t::get(secondary, itr->secondary_key);

               return itr_cache.add( *itr );
            }

            int end_secondary( uint64_t code, uint64_t scope, uint64_t table ) {
               auto tab = context.find_table( code, scope, table );
               if( !tab ) return -1;

               return itr_cache.cache_table( *tab );
            }

            int next_secondary( int iterator, uint64_t& primary ) {
               if( iterator < -1 ) return -1; // cannot increment past end iterator of index

               const auto& obj = itr_cache.get(iterator); // Check for iterator != -1 happens in this call
               const auto& idx = context.db.get_index<typename chainbase::get_index_type<ObjectType>::type, contracts::by_secondary>();

               auto itr = idx.iterator_to(obj);
               ++itr;

               if( itr == idx.end() || itr->t_id != obj.t_id ) return itr_cache.get_end_iterator_by_table_id(obj.t_id);

               primary = itr->primary_key;
               return itr_cache.add(*itr);
            }

            int previous_secondary( int iterator, uint64_t& primary ) {
               const auto& idx = context.db.get_index<typename chainbase::get_index_type<ObjectType>::type, contracts::by_secondary>();

               if( iterator < -1 ) // is end iterator
               {
                  auto tab = itr_cache.find_table_by_end_iterator(iterator);
                  FC_ASSERT( tab, "not a valid end iterator" );

                  auto itr = idx.upper_bound(tab->id);
                  if( idx.begin() == idx.end() || itr == idx.begin() ) return -1; // Empty index

                  --itr;

                  if( itr->t_id != tab->id ) return -1; // Empty index

                  primary = itr->primary_key;
                  return itr_cache.add(*itr);
               }

               const auto& obj = itr_cache.get(iterator); // Check for iterator != -1 happens in this call

               auto itr = idx.iterator_to(obj);
               if( itr == idx.begin() ) return -1; // cannot decrement past beginning iterator of index

               --itr;

               if( itr->t_id != obj.t_id ) return -1; // cannot decrement past beginning iterator of index

               primary = itr->primary_key;
               return itr_cache.add(*itr);
            }

            int find_primary( uint64_t code, uint64_t scope, uint64_t table, secondary_key_proxy_type secondary, uint64_t primary ) {
               auto tab = context.find_table( code, scope, table );
               if( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto* obj = context.db.find<ObjectType, contracts::by_primary>( boost::make_tuple( tab->id, primary ) );
               if( !obj ) return table_end_itr;
               secondary_key_helper_t::get(secondary, obj->secondary_key);

               return itr_cache.add( *obj );
            }

            int lowerbound_primary( uint64_t code, uint64_t scope, uint64_t table, uint64_t primary ) {
               auto tab = context.find_table( code, scope, table );
               if (!tab) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto& idx = context.db.get_index<typename chainbase::get_index_type<ObjectType>::type, contracts::by_primary>();
               auto itr = idx.lower_bound(boost::make_tuple(tab->id, primary));
               if (itr == idx.end()) return table_end_itr;
               if (itr->t_id != tab->id) return table_end_itr;

               return itr_cache.add(*itr);
            }

            int upperbound_primary( uint64_t code, uint64_t scope, uint64_t table, uint64_t primary ) {
               auto tab = context.find_table( code, scope, table );
               if ( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto& idx = context.db.get_index<typename chainbase::get_index_type<ObjectType>::type, contracts::by_primary>();
               auto itr = idx.upper_bound(boost::make_tuple(tab->id, primary));
               if (itr == idx.end()) return table_end_itr;
               if (itr->t_id != tab->id) return table_end_itr;

               itr_cache.cache_table(*tab);
               return itr_cache.add(*itr);
            }

            int next_primary( int iterator, uint64_t& primary ) {
               if( iterator < -1 ) return -1; // cannot increment past end iterator of table

               const auto& obj = itr_cache.get(iterator); // Check for iterator != -1 happens in this call
               const auto& idx = context.db.get_index<typename chainbase::get_index_type<ObjectType>::type, contracts::by_primary>();

               auto itr = idx.iterator_to(obj);
               ++itr;

               if( itr == idx.end() || itr->t_id != obj.t_id ) return itr_cache.get_end_iterator_by_table_id(obj.t_id);

               primary = itr->primary_key;
               return itr_cache.add(*itr);
            }

            int previous_primary( int iterator, uint64_t& primary ) {
               const auto& idx = context.db.get_index<typename chainbase::get_index_type<ObjectType>::type, contracts::by_primary>();

               if( iterator < -1 ) // is end iterator
               {
                  auto tab = itr_cache.find_table_by_end_iterator(iterator);
                  FC_ASSERT( tab, "not a valid end iterator" );

                  auto itr = idx.upper_bound(tab->id);
                  if( idx.begin() == idx.end() || itr == idx.begin() ) return -1; // Empty table

                  --itr;

                  if( itr->t_id != tab->id ) return -1; // Empty table

                  primary = itr->primary_key;
                  return itr_cache.add(*itr);
               }

               const auto& obj = itr_cache.get(iterator); // Check for iterator != -1 happens in this call

               auto itr = idx.iterator_to(obj);
               if( itr == idx.begin() ) return -1; // cannot decrement past beginning iterator of table

               --itr;

               if( itr->t_id != obj.t_id ) return -1; // cannot decrement past beginning iterator of index

               primary = itr->primary_key;
               return itr_cache.add(*itr);
            }

            void get( int iterator, uint64_t& primary, secondary_key_proxy_type secondary ) {
               const auto& obj = itr_cache.get( iterator );
               primary   = obj.primary_key;
               secondary_key_helper_t::get(secondary, obj.secondary_key);
            }

         private:
            database_api&              context;
            iterator_cache<ObjectType>  itr_cache;
      };




   private:
      database_api(const action& a);

   public:
      static database_api *_instance;
      static database_api& get() {
         static action act;
         if (!_instance) {
            _instance = new database_api(act);
         }
         return *_instance;
      }

      void get_code(uint64_t account, string& code);

      /**
       * @brief Require @ref account to have approved of this message
       * @param account The account whose approval is required
       *
       * This method will check that @ref account is listed in the message's declared authorizations, and marks the
       * authorization as used. Note that all authorizations on a message must be used, or the message is invalid.
       *
       * @throws tx_missing_auth If no sufficient permission was found
       */
      void require_authorization(const account_name& account);
      bool has_authorization(const account_name& account) const;
      void require_authorization(const account_name& account, const permission_name& permission);
      void require_write_lock(const scope_name& scope);
      void require_read_lock(const account_name& account, const scope_name& scope);

      /**
       * @return true if account exists, false if it does not
       */
      bool is_account(const account_name& account)const;

      /**
       * Requires that the current action be delivered to account
       */
      void require_recipient(account_name account);

      /**
       * Return true if the current action has already been scheduled to be
       * delivered to the specified account.
       */
      bool has_recipient(account_name account)const;

      bool                     all_authorizations_used()const;
      vector<permission_level> unused_authorizations()const;

      vector<account_name> get_active_producers() const;

      const bytes&         get_packed_transaction();

      chainbase::database    db;  ///< database where state is stored
      const action&                 act; ///< message being applied
      account_name                  receiver; ///< the code that is currently running
      bool                          privileged   = false;
      bool                          context_free = false;
      bool                          used_context_free_api = false;

      ///< Parallel to act.authorization; tracks which permissions have been used while processing the message
      vector<bool> used_authorizations;

      struct apply_results {
         vector<action_trace> applied_actions;
         vector<fc::static_variant<deferred_transaction, deferred_reference>> deferred_transaction_requests;
         size_t deferred_transactions_count = 0;
      };

      apply_results results;

      template<typename T>
      void console_append(T val) {
         _pending_console_output << val;
      }

      template<typename T, typename ...Ts>
      void console_append(T val, Ts ...rest) {
         console_append(val);
         console_append(rest...);
      };

      inline void console_append_formatted(const string& fmt, const variant_object& vo) {
         console_append(fc::format_string(fmt, vo));
      }

      void checktime(uint32_t instruction_count);

      int get_action( uint32_t type, uint32_t index, char* buffer, size_t buffer_size )const;
      int get_context_free_data( uint32_t index, char* buffer, size_t buffer_size )const;

      void update_db_usage( const account_name& payer, int64_t delta );

      int  db_store_i64( uint64_t scope, uint64_t table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size );
      void db_update_i64( int iterator, account_name payer, const char* buffer, size_t buffer_size );
      void db_remove_i64( int iterator );
      int db_get_i64( int iterator, char* buffer, size_t buffer_size );
      int db_next_i64( int iterator, uint64_t& primary );
      int db_previous_i64( int iterator, uint64_t& primary );
      int db_find_i64( uint64_t code, uint64_t scope, uint64_t table, uint64_t id );
      int db_lowerbound_i64( uint64_t code, uint64_t scope, uint64_t table, uint64_t id );
      int db_upperbound_i64( uint64_t code, uint64_t scope, uint64_t table, uint64_t id );
      int db_end_i64( uint64_t code, uint64_t scope, uint64_t table );

      generic_index<contracts::index64_object>    idx64;
      generic_index<contracts::index128_object>   idx128;
      generic_index<contracts::index256_object, uint128_t*, const uint128_t*>   idx256;
      generic_index<contracts::index_double_object> idx_double;

      static constexpr uint32_t base_row_fee = 200;



#define DB_API_METHOD_WRAPPERS_SIMPLE_SECONDARY_DEF(IDX, TYPE)\
      int db_##IDX##_find_secondary( uint64_t code, uint64_t scope, uint64_t table, const TYPE& secondary, uint64_t& primary ); \
      int db_##IDX##_find_primary( uint64_t code, uint64_t scope, uint64_t table, TYPE& secondary, uint64_t primary );\
      int db_##IDX##_lowerbound( uint64_t code, uint64_t scope, uint64_t table,  TYPE& secondary, uint64_t& primary );\
      int db_##IDX##_upperbound( uint64_t code, uint64_t scope, uint64_t table,  TYPE& secondary, uint64_t& primary );\
      int db_##IDX##_end( uint64_t code, uint64_t scope, uint64_t table );\
      int db_##IDX##_next( int iterator, uint64_t& primary  );\
      int db_##IDX##_previous( int iterator, uint64_t& primary );

#define DB_API_METHOD_WRAPPERS_ARRAY_SECONDARY_DEF(IDX, ARR_SIZE, ARR_ELEMENT_TYPE)\
      int db_##IDX##_store( uint64_t scope, uint64_t table, uint64_t payer, uint64_t id, array_ptr<const ARR_ELEMENT_TYPE> data, size_t data_len);\
      void db_##IDX##_update( int iterator, uint64_t payer, array_ptr<const ARR_ELEMENT_TYPE> data, size_t data_len );\
      void db_##IDX##_remove( int iterator );\
      int db_##IDX##_find_secondary( uint64_t code, uint64_t scope, uint64_t table, array_ptr<const ARR_ELEMENT_TYPE> data, size_t data_len, uint64_t& primary );\
      int db_##IDX##_find_primary( uint64_t code, uint64_t scope, uint64_t table, array_ptr<ARR_ELEMENT_TYPE> data, size_t data_len, uint64_t primary );\
      int db_##IDX##_lowerbound( uint64_t code, uint64_t scope, uint64_t table, array_ptr<ARR_ELEMENT_TYPE> data, size_t data_len, uint64_t& primary );\
      int db_##IDX##_upperbound( uint64_t code, uint64_t scope, uint64_t table, array_ptr<ARR_ELEMENT_TYPE> data, size_t data_len, uint64_t& primary );\
      int db_##IDX##_end( uint64_t code, uint64_t scope, uint64_t table );\
      int db_##IDX##_next( int iterator, uint64_t& primary  );\
      int db_##IDX##_previous( int iterator, uint64_t& primary );


#define DB_API_METHOD_WRAPPERS_FLOAT_SECONDARY_DEF(IDX, TYPE)\
      int db_##IDX##_find_secondary( uint64_t code, uint64_t scope, uint64_t table, const char* secondary, size_t data_len, uint64_t* primary ); \
      int db_##IDX##_find_primary( uint64_t code, uint64_t scope, uint64_t table, char* secondary, size_t data_len, uint64_t primary ); \
      int db_##IDX##_lowerbound( uint64_t code, uint64_t scope, uint64_t table,  char* secondary, size_t data_len, uint64_t* primary ); \
      int db_##IDX##_upperbound( uint64_t code, uint64_t scope, uint64_t table,  char* secondary, size_t data_len, uint64_t* primary ); \
      int db_##IDX##_end( uint64_t code, uint64_t scope, uint64_t table ); \
      int db_##IDX##_next( int iterator, uint64_t* primary  ); \
      int db_##IDX##_previous( int iterator, uint64_t* primary );

DB_API_METHOD_WRAPPERS_SIMPLE_SECONDARY_DEF(idx64,  uint64_t)
DB_API_METHOD_WRAPPERS_SIMPLE_SECONDARY_DEF(idx128, uint128_t)
DB_API_METHOD_WRAPPERS_ARRAY_SECONDARY_DEF(idx256, 2, uint128_t)
DB_API_METHOD_WRAPPERS_FLOAT_SECONDARY_DEF(idx_double, uint64_t)


   private:
      iterator_cache<key_value_object> keyval_cache;

      void append_results(apply_results &&other) {
         fc::move_append(results.applied_actions, std::move(other.applied_actions));
         fc::move_append(results.deferred_transaction_requests, std::move(other.deferred_transaction_requests));
         results.deferred_transactions_count += other.deferred_transactions_count;
      }

      const name& get_receiver();
      using table_id_object = contracts::table_id_object;
      const table_id_object* find_table( name code, name scope, name table );
      const table_id_object& find_or_create_table( name code, name scope, name table, const account_name &payer );
      void remove_table( const table_id_object& tid );

      int  db_store_i64( uint64_t code, uint64_t scope, uint64_t table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size );

      vector<account_name>                _notified; ///< keeps track of new accounts to be notifed of current message
      vector<action>                      _inline_actions; ///< queued inline messages
      vector<action>                      _cfa_inline_actions; ///< queued inline messages
      std::ostringstream                  _pending_console_output;

      vector<shard_lock>                  _read_locks;
      vector<scope_name>                  _write_scopes;
      bytes                               _cached_trx;
      uint64_t                            _cpu_usage;
};

class softfloat_api {
   public:
      // TODO add traps on truncations for special cases (NaN or outside the range which rounds to an integer)
//      using context_aware_api::context_aware_api;
      // float binops
      static bool is_nan( const float32_t f ) {
         return ((f.v & 0x7FFFFFFF) > 0x7F800000);
      }
      static bool is_nan( const float64_t f ) {
         return ((f.v & 0x7FFFFFFFFFFFFFFF) > 0x7FF0000000000000);
      }
      static bool is_nan( const float128_t& f ) {
         const uint32_t* iptr = (const uint32_t*)&f;
         return softfloat_isNaNF128M( iptr );
      }
      static float32_t to_softfloat32( float f ) {
         return *reinterpret_cast<float32_t*>(&f);
      }
      static float64_t to_softfloat64( double d ) {
         return *reinterpret_cast<float64_t*>(&d);
      }
      static float from_softfloat32( float32_t f ) {
         return *reinterpret_cast<float*>(&f);
      }
      static double from_softfloat64( float64_t d ) {
         return *reinterpret_cast<double*>(&d);
      }
      static constexpr uint32_t inv_float_eps = 0x4B000000;
      static constexpr uint64_t inv_double_eps = 0x4330000000000000;

      static bool sign_bit( float32_t f ) { return f.v >> 31; }
      static bool sign_bit( float64_t f ) { return f.v >> 63; }
};

} } // namespace eosio::chain

//FC_REFLECT(eosio::chain::database_api::apply_results, (applied_actions)(deferred_transaction_requests)(deferred_transactions_count))