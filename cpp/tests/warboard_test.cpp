#include "nw_network/nw_network.hpp"
#include <cassert>
using namespace nw::network;
int main(){
  WarboardStats s;s.counter=40;s.local_index=3;
  s.local.mask=(1ull<<8)|(1ull<<9)|(1ull<<25)|(1ull<<28)|(1ull<<29);
  s.local.values={65117,19542,2,8,1167};
  s.blocks.push_back({{{17,{(1ull<<1)|(1ull<<2),{4296,509923}}}}});
  WriteBuffer w;marshal_warboard(s,w);ReadBuffer r(w.span());auto d=unmarshal_warboard(r);
  assert(r.empty());assert(d.counter==40&&d.local_index==3);assert(d.local.stat(8)==65117);assert(d.blocks[0].players[0].row.stat(2)==509923);
  bool rejected=false;try{WriteBuffer bad;marshal_vlq_u64(bad,3);marshal_vlq_u64(bad,7);ReadBuffer br(bad.span());(void)unmarshal_warboard_row(br);}catch(const ProtocolError&){rejected=true;}assert(rejected);
}
