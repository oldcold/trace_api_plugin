#pragma once

#include <eosio/trace_api/common.hpp>
#include <eosio/trace_api/trace.hpp>
#include <exception>
#include <functional>
#include <map>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/block_state.hpp>

namespace eosio { namespace trace_api {

using chain::transaction_id_type;
using chain::packed_transaction;

template <typename StoreProvider>
class chain_extraction_impl_type {
public:
   /**
    * Chain Extractor for capturing transaction traces, action traces, and block info.
    * @param store provider of append & append_lib
    * @param except_handler called on exceptions, logging if any is left to the user
    */
   chain_extraction_impl_type( StoreProvider store, exception_handler except_handler)
   : store(std::move(store))
   , except_handler(std::move(except_handler))
   {}

   chain_plugin *chain_plug = nullptr;

   /// connect to chain controller applied_transaction signal
   void signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::signed_transaction& strx ) {
      on_applied_transaction( trace, strx );
   }

   /// connect to chain controller accepted_block signal
   void signal_accepted_block( const chain::block_state_ptr& bsp ) {
      on_accepted_block( bsp );
   }

   /// connect to chain controller irreversible_block signal
   void signal_irreversible_block( const chain::block_state_ptr& bsp ) {
      on_irreversible_block( bsp );
   }

private:
   static bool is_onblock(const chain::transaction_trace_ptr& p) {
      if (p->action_traces.size() != 1)
         return false;
      const auto& act = p->action_traces[0].act;
      if (act.account != eosio::chain::config::system_account_name || act.name != N(onblock) ||
          act.authorization.size() != 1)
         return false;
      const auto& auth = act.authorization[0];
      return auth.actor == eosio::chain::config::system_account_name &&
             auth.permission == eosio::chain::config::active_name;
   }

   void on_applied_transaction(const chain::transaction_trace_ptr& trace, const chain::signed_transaction& t) {
      if( !trace->receipt ) return;
      // include only executed transactions; soft_fail included so that onerror (and any inlines via onerror) are included
      if((trace->receipt->status != chain::transaction_receipt_header::executed &&
          trace->receipt->status != chain::transaction_receipt_header::soft_fail)) {
         return;
      }
      if( is_onblock( trace )) {
         onblock_trace.emplace( trace );
      } else if( trace->failed_dtrx_trace ) {
         cached_traces[trace->failed_dtrx_trace->id] = std::pair{trace, t};
      } else {
         cached_traces[trace->id] = std::pair{trace, t};
      }
   }

   void on_accepted_block(const chain::block_state_ptr& block_state) {
      store_block_trace( block_state );
   }

   void on_irreversible_block( const chain::block_state_ptr& block_state ) {
      store_lib( block_state );
   }

   void store_block_trace( const chain::block_state_ptr& block_state ) {
      try {
         block_trace_v0 bt = create_block_trace_v0( block_state );

         std::vector<transaction_trace_v0>& traces = bt.transactions;
         traces.reserve( block_state->block->transactions.size() + 1 );
         if( onblock_trace )
            traces.emplace_back( to_transaction_trace_v0( *onblock_trace));
         for( const auto& r : block_state->block->transactions ) {
            transaction_id_type id;
            if( r.trx.contains<transaction_id_type>()) {
               id = r.trx.get<transaction_id_type>();
            } else {
               id = r.trx.get<packed_transaction>().id();
            }
            const auto it = cached_traces.find( id );
            if( it != cached_traces.end() ) {
               traces.emplace_back( to_transaction_trace_v0( it->second.first, it->second.second));
            }
         }
         cached_traces.clear();
         onblock_trace.reset();

         store.append( std::move( bt ) );

      } catch( ... ) {
         except_handler( MAKE_EXCEPTION_WITH_CONTEXT( std::current_exception() ) );
      }
   }

   void store_lib( const chain::block_state_ptr& bsp ) {
      try {
         store.append_lib( bsp->block_num );
      } catch( ... ) {
         except_handler( MAKE_EXCEPTION_WITH_CONTEXT( std::current_exception() ) );
      }
   }


   template<typename T>
   fc::variant to_variant_with_abi( const T& obj ) {
      fc::variant pretty_output;
      auto max_time = chain_plug->get_abi_serializer_max_time();
      chain::abi_serializer::to_variant(
              obj,
              pretty_output,
              [&]( chain::account_name n ){ return chain_plug->chain().get_abi_serializer(n, max_time); },
              max_time
      );
      return pretty_output;
   }

private:
   StoreProvider                                                                                      store;
   exception_handler                                                                                  except_handler;
   std::map<transaction_id_type, std::pair<chain::transaction_trace_ptr, chain::signed_transaction>>  cached_traces;
   fc::optional<chain::transaction_trace_ptr>                                                         onblock_trace;

   action_trace_v0 to_action_trace_v0( const chain::action_trace& at ) {
      action_trace_v0 r;
      r.receiver = at.receiver;
      r.account = at.act.account;
      r.action = at.act.name;
      r.data = to_variant_with_abi(at)["act"]["data"];
      if( at.receipt ) {
         r.global_sequence = at.receipt->global_sequence;
      }
      r.authorization.reserve( at.act.authorization.size());
      for( const auto& auth : at.act.authorization ) {
         r.authorization.emplace_back( authorization_trace_v0{auth.actor, auth.permission} );
      }
      return r;
   }

/// @return transaction_trace_v0 with populated action_trace_v0
    transaction_trace_v0 to_transaction_trace_v0( const chain::transaction_trace_ptr& t, const chain::signed_transaction& trx = {}) {
       transaction_trace_v0 r;
       if( !t->failed_dtrx_trace ) {
          r.id = t->id;
          r.status = t->receipt->status;
          r.cpu_usage_us = t->receipt->cpu_usage_us;
          r.net_usage_words = t->receipt->net_usage_words;
          r.signatures = trx.signatures;
          r.trx_header = chain::transaction_header{
              trx.expiration,
              trx.ref_block_num,
              trx.ref_block_prefix,
              trx.max_net_usage_words,
              trx.max_cpu_usage_ms,
              trx.delay_sec
          };
       } else {
          r.id = t->failed_dtrx_trace->id; // report the failed trx id since that is the id known to user
       }
       r.actions.reserve( t->action_traces.size());
       for( const auto& at : t->action_traces ) {
          if( !at.context_free ) { // not including CFA at this time
             r.actions.emplace_back( to_action_trace_v0( at ));
          }
       }
       return r;
    }

/// @return block_trace_v0 without any transaction_trace_v0
    block_trace_v0 create_block_trace_v0( const chain::block_state_ptr& bsp ) {
       block_trace_v0 r;
       r.id = bsp->id;
       r.number = bsp->block_num;
       r.previous_id = bsp->block->previous;
       r.timestamp = bsp->block->timestamp;
       r.producer = bsp->block->producer;
       r.schedule_version = bsp->block->schedule_version;
       r.transaction_mroot = bsp->block->transaction_mroot;
       r.action_mroot = bsp->block->action_mroot;
       return r;
    }

};

}}