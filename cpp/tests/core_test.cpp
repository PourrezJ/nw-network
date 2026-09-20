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
  bool threw=false;try{std::array<std::uint8_t,1>b{2};ReadBuffer bad(b);(void)Marshaler<bool>::unmarshal(bad);}catch(const ProtocolError&){threw=true;}assert(threw);
}
