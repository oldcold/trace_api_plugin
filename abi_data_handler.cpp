#include <eosio/trace_api/abi_data_handler.hpp>
#include <eosio/chain/abi_serializer.hpp>

namespace eosio::trace_api {

   void abi_data_handler::add_abi( const chain::name& name, const chain::abi_def& abi ) {
      abi_serializer_by_account.emplace(name, std::make_shared<chain::abi_serializer>(abi, fc::microseconds::maximum()));
   }

   fc::variant abi_data_handler::process_data(const action_trace_v0& action, const yield_function& yield ) {
       return action.data;
   }
}