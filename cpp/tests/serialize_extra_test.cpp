#include "nw_network/serialize_extra.hpp"
#include <array>
#include <cassert>
#include <cmath>
using namespace nw::network;

int main(){
  {
    WriteBuffer w;
    Vec3CompMarshaler::marshal(Vec3{1.0f,2.0f,3.0f},w);
    const std::vector<std::uint8_t> expected{0x3c,0x00,0x40,0x00,0x42,0x00};
    assert(w.bytes()==expected);
    ReadBuffer r(w.span());
    assert(Vec3CompMarshaler::unmarshal(r)==Vec3{1.0f,2.0f,3.0f});
    assert(r.empty());
  }
  {
    WriteBuffer w;NonUniformScaleCompMarshaler::marshal(Vec3{1,1,1},w);
    assert(w.bytes()==std::vector<std::uint8_t>({0}));
    WriteBuffer u;NonUniformScaleCompMarshaler::marshal(Vec3{2,2,2},u);
    assert(u.bytes()==std::vector<std::uint8_t>({1,0x40,0x00}));
    WriteBuffer f;NonUniformScaleCompMarshaler::marshal(Vec3{1,2,3},f);
    assert(f.bytes()==std::vector<std::uint8_t>({2,0x3c,0,0x40,0,0x42,0}));
  }
  {
    BitSet<1> bs;bs.set(0,true);bs.set(7,true);bs.set(63,true);
    assert(bs.count()==3);
    WriteBuffer w;Marshaler<BitSet<1>>::marshal(bs,w);
    assert(w.bytes()==std::vector<std::uint8_t>({0x80,0,0,0,0,0,0,0x81}));
    ReadBuffer r(w.span());assert(Marshaler<BitSet<1>>::unmarshal(r)==bs);
  }
  {
    PackedSize p(4,3);WriteBuffer w;Marshaler<PackedSize>::marshal(p,w);
    assert(w.bytes()==std::vector<std::uint8_t>({0x23}));
    ReadBuffer r(w.span());auto q=Marshaler<PackedSize>::unmarshal(r);
    assert(q.bytes()==4&&q.additional_bits()==3&&q.total_size_in_bits()==35);
  }
  {
    Float16Marshaler codec(0.0f,1.0f);WriteBuffer w;codec.marshal(0.5f,w);
    assert(w.size()==2);ReadBuffer r(w.span());assert(std::abs(codec.unmarshal(r)-0.5f)<1.0f/65535.0f);
  }
  {
    WriteBuffer w;IntegerQuantizationMarshalerU8<0,100>::marshal(50,w);
    assert(w.bytes()==std::vector<std::uint8_t>({127}));
    ReadBuffer r(w.span());assert(std::abs(IntegerQuantizationMarshalerU8<0,100>::unmarshal(r)-50)<=1);
  }
  {
    DeltaCompressedCounterHandler h;h.set_value(0x1234);assert(h.value()==0x1234);
    assert(h.absolute_portion.value()&&*h.absolute_portion.value()==0x1200);
    assert(h.relative_portion.value()&&*h.relative_portion.value()==0x34);
    h.set_value(0x12ff);assert(h.value()==0x12ff);
  }
  {
    QuantizedRelativePosition q;assert(q.is_zero());
    q.quantized_values={1,2,3};WriteBuffer w;Marshaler<QuantizedRelativePosition>::marshal(q,w);
    assert(w.bytes()==std::vector<std::uint8_t>({1,2,3}));
  }
}
