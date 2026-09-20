#include "nw_network/nw_network.hpp"
#include <array>
#include <cassert>
using namespace nw::network;
int main(){
  constexpr std::array<std::uint8_t,9> check{'1','2','3','4','5','6','7','8','9'};
  static_assert(Crc32::compute(check)==0xcbf43926u);
  for(auto v:std::array<std::uint32_t,8>{0u,0x7fu,0x80u,0x3fffu,0x4000u,0x1fffffu,0x10000000u,0xffffffffu}){
    WriteBuffer w;marshal_vlq_u32(w,v);ReadBuffer r(w.span());assert(unmarshal_vlq_u32(r)==v);assert(r.empty());
  }
  for(auto v:std::array<std::uint64_t,8>{0ull,0x7full,0x80ull,0x3fffull,0x10000000ull,0x800000000ull,0x100000000000000ull,0xffffffffffffffffull}){
    WriteBuffer w;marshal_vlq_u64(w,v);ReadBuffer r(w.span());assert(unmarshal_vlq_u64(r)==v);assert(r.empty());
  }
  WriteBuffer w;Marshaler<std::string>::marshal("hello",w);ReadBuffer r(w.span());assert(Marshaler<std::string>::unmarshal(r)=="hello");assert(r.empty());
  ReplicatedFieldHandler<std::uint32_t> field;
  field.set_default_value(7);
  assert(field.has_value());
  assert(field.is_default_value());
  assert(!field.last_modified().is_valid());
  field.set_value(8);
  assert(field.last_modified()==SequenceNumber::valid_non_sequence());
  field.set_value(7);
  assert(field.is_default_value());
  assert(!field.last_modified().is_valid());

  ReplicatedFieldHandler<std::uint32_t> old_field;
  old_field.set_value(10);
  old_field.set_last_modified(SequenceNumber::seq(3));
  ReplicatedFieldHandler<std::uint32_t> incoming;
  incoming.set_value(11);
  ReplicatedFieldHandler<std::uint32_t> merged;
  assert(merged.merge_and_update_sequence(old_field,incoming,SequenceNumber::seq(4),true));
  assert(merged.value()&&*merged.value()==11);
  assert(merged.last_modified()==SequenceNumber::seq(4));
  assert(merged.has_new_network_data());

  bool threw=false;try{std::array<std::uint8_t,1>b{2};ReadBuffer bad(b);(void)Marshaler<bool>::unmarshal(bad);}catch(const ProtocolError&){threw=true;}assert(threw);
}
