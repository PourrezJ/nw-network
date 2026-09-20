#include "nw_network/nw_network.hpp"
#include <cassert>
using namespace nw::network;
int main(){
  auto c=ReplicatedContainer<std::vector<std::uint8_t>>::snapshot(SequenceNumber::seq(7),{10,20});
  WriteBuffer w;c.marshal(w);
  const std::vector<std::uint8_t> expected{0,8,2,10,20};assert(w.bytes()==expected);
  ReadBuffer r(w.span());auto decoded=ReplicatedContainer<std::vector<std::uint8_t>>::unmarshal(r);assert(r.empty());assert(decoded.values()==std::vector<std::uint8_t>({10,20}));
  using RC=ReplicatedContainer<std::vector<std::uint8_t>>;using Ch=Change<VlqU64,std::uint8_t>;
  auto delta=RC::delta({Ch::update({0},7,SequenceNumber::seq(5)),Ch::remove({1},SequenceNumber::seq(5))});
  WriteBuffer dw;delta.marshal(dw);ReadBuffer dr(dw.span());auto dd=RC::unmarshal(dr);assert(dr.empty());assert(dd.current_changes().size()==2);assert(dd.current_changes()[0].sequence==dd.current_changes()[1].sequence);
}
