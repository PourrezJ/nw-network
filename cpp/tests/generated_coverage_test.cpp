#include "nw_network/nw_network.hpp"
#include "nw_network/generated_states.hpp"
#include "nw_network/generated_messages.hpp"
#include <cassert>
#include <set>
using namespace nw::network;
int main(){
  auto names=exported_state_names();auto ids=exported_state_type_indices();
  assert(!names.empty());assert(names.size()==ids.size());
  std::set<std::uint32_t> unique;
  for(std::size_t i=0;i<names.size();++i){assert(!names[i].empty());assert(type_by_index(ids[i])!=nullptr);unique.insert(ids[i]);}
  assert(unique.size()==ids.size());
  assert(generated::state_factory_count()==ids.size());
  assert(generated::message_factory_count()>0);
}
