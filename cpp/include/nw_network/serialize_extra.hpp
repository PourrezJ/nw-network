#pragma once
#include "nw_network/nw_network.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>

namespace nw::network {

struct Sentinel {
  std::optional<std::uint32_t> value;
  static Sentinel some(std::uint32_t v){return Sentinel{v};}
  bool operator==(const Sentinel&)const=default;
};
template<>struct Marshaler<Sentinel>{
  static void marshal(const Sentinel&v,WriteBuffer&w){Marshaler<std::uint32_t>::marshal(v.value.value_or(UINT32_MAX),w);}
  static Sentinel unmarshal(ReadBuffer&r){auto v=Marshaler<std::uint32_t>::unmarshal(r);return Sentinel{v==UINT32_MAX?std::nullopt:std::optional<std::uint32_t>{v}};}
};

struct RawSequenceNumber{
  std::uint64_t value{};
  bool operator==(const RawSequenceNumber&)const=default;
};
template<>struct Marshaler<RawSequenceNumber>{
  static void marshal(RawSequenceNumber v,WriteBuffer&w){
    std::array<std::uint8_t,8> bytes{};
    std::memcpy(bytes.data(),&v.value,8);
    w.write_bytes(bytes);
  }
  static RawSequenceNumber unmarshal(ReadBuffer&r){
    auto bytes=r.read_bytes(8);std::uint64_t value{};std::memcpy(&value,bytes.data(),8);return{value};
  }
};

inline std::uint16_t f32_to_f16_bits(float value) noexcept {
  const std::uint32_t x=std::bit_cast<std::uint32_t>(value);
  const std::uint16_t sign=static_cast<std::uint16_t>((x>>16)&0x8000u);
  const std::uint32_t exp=(x>>23)&0xffu;
  std::uint32_t mant=x&0x7fffffu;
  if(exp==0xffu){
    if(mant==0)return static_cast<std::uint16_t>(sign|0x7c00u);
    return static_cast<std::uint16_t>(sign|0x7e00u);
  }
  int half_exp=static_cast<int>(exp)-127+15;
  if(half_exp>=31)return static_cast<std::uint16_t>(sign|0x7c00u);
  if(half_exp<=0){
    if(half_exp<-10)return sign;
    mant|=0x800000u;
    const int shift=14-half_exp;
    const std::uint32_t bias=(1u<<(shift-1))-1u+((mant>>shift)&1u);
    return static_cast<std::uint16_t>(sign|((mant+bias)>>shift));
  }
  mant+=0xfffu+((mant>>13)&1u);
  if(mant&0x800000u){
    mant=0;
    ++half_exp;
    if(half_exp>=31)return static_cast<std::uint16_t>(sign|0x7c00u);
  }
  return static_cast<std::uint16_t>(sign|(static_cast<std::uint16_t>(half_exp)<<10)|static_cast<std::uint16_t>(mant>>13));
}
inline float f16_bits_to_f32(std::uint16_t value) noexcept {
  const std::uint32_t sign=static_cast<std::uint32_t>(value&0x8000u)<<16;
  std::uint32_t exp=(value>>10)&0x1fu;
  std::uint32_t mant=value&0x03ffu;
  std::uint32_t out{};
  if(exp==0){
    if(mant==0)out=sign;
    else{
      int e=-14;
      while((mant&0x0400u)==0){mant<<=1;--e;}
      mant&=0x03ffu;
      out=sign|static_cast<std::uint32_t>(e+127)<<23|(mant<<13);
    }
  }else if(exp==31){
    out=sign|0x7f800000u|(mant<<13);
  }else{
    out=sign|((exp+112u)<<23)|(mant<<13);
  }
  return std::bit_cast<float>(out);
}

struct HalfF32{float value{};bool operator==(const HalfF32&)const=default;};
template<>struct Marshaler<HalfF32>{
  static void marshal(HalfF32 v,WriteBuffer&w){w.write_int<std::uint16_t>(f32_to_f16_bits(v.value));}
  static HalfF32 unmarshal(ReadBuffer&r){return{f16_bits_to_f32(r.read_int<std::uint16_t>())};}
};
struct HalfF32Marshaler{
  static void marshal(float value,WriteBuffer&w){Marshaler<HalfF32>::marshal(HalfF32{value},w);}
  static float unmarshal(ReadBuffer&r){return Marshaler<HalfF32>::unmarshal(r).value;}
};

struct Vec2{
  float x{},y{};
  bool operator==(const Vec2&)const=default;
};
struct Quat{
  float x{},y{},z{},w{1.0f};
  bool operator==(const Quat&)const=default;
};
using HalfVec3=std::array<float,3>;

struct HalfVec3Marshaler{
  static void marshal(const HalfVec3&v,WriteBuffer&w){for(float x:v)HalfF32Marshaler::marshal(x,w);}
  static HalfVec3 unmarshal(ReadBuffer&r){return{HalfF32Marshaler::unmarshal(r),HalfF32Marshaler::unmarshal(r),HalfF32Marshaler::unmarshal(r)};}
};

template<std::size_t WORDS>struct BitSet{
  std::array<std::uint64_t,WORDS> words{};
  static constexpr std::size_t BITS=WORDS*64;
  [[nodiscard]]bool get(std::size_t index)const noexcept{auto wi=index/64,bi=index%64;return wi<WORDS&&(words[wi]&(1ull<<bi))!=0;}
  void set(std::size_t index,bool value)noexcept{auto wi=index/64,bi=index%64;if(wi>=WORDS)return;if(value)words[wi]|=1ull<<bi;else words[wi]&=~(1ull<<bi);}
  [[nodiscard]]std::uint32_t count()const noexcept{std::uint32_t n=0;for(auto w:words)n+=std::popcount(w);return n;}
  bool operator==(const BitSet&)const=default;
};
template<std::size_t WORDS>struct Marshaler<BitSet<WORDS>>{
  static void marshal(const BitSet<WORDS>&v,WriteBuffer&w){for(auto word:v.words)Marshaler<std::uint64_t>::marshal(word,w);}
  static BitSet<WORDS> unmarshal(ReadBuffer&r){BitSet<WORDS>v;for(auto&word:v.words)word=Marshaler<std::uint64_t>::unmarshal(r);return v;}
};

struct IntegerOmitLowerByteMarshaler{
  static void marshal(std::uint16_t value,WriteBuffer&w){w.write_u8(static_cast<std::uint8_t>(value>>8));}
  static std::uint16_t unmarshal(ReadBuffer&r){return static_cast<std::uint16_t>(r.read_u8())<<8;}
};
struct DeltaIntegerMarshaler{
  static void marshal(std::uint16_t value,WriteBuffer&w){w.write_u8(static_cast<std::uint8_t>(value&0xff));}
  static std::uint16_t unmarshal_u16(ReadBuffer&r){return r.read_u8();}
  static void marshal(std::uint32_t value,WriteBuffer&w){w.write_u8(static_cast<std::uint8_t>(value&0xff));}
  static std::uint32_t unmarshal_u32(ReadBuffer&r){return r.read_u8();}
};

struct QuantizedRelativePosition{
  std::array<std::uint8_t,3> quantized_values{255,255,255};
  [[nodiscard]]bool is_zero()const noexcept{return quantized_values==std::array<std::uint8_t,3>{255,255,255};}
  bool operator==(const QuantizedRelativePosition&)const=default;
};
template<>struct Marshaler<QuantizedRelativePosition>{
  static void marshal(const QuantizedRelativePosition&v,WriteBuffer&w){for(auto x:v.quantized_values)w.write_u8(x);}
  static QuantizedRelativePosition unmarshal(ReadBuffer&r){return{{r.read_u8(),r.read_u8(),r.read_u8()}};}
};
inline std::uint8_t quantize_with_range(float value,float delta_range){
  const float q=(value+delta_range)*255.0f/(2.0f*delta_range);
  return static_cast<std::uint8_t>(std::clamp(q,0.0f,255.0f));
}
inline float unquantize_with_range(std::uint8_t q,float delta_range){
  return 2.0f*delta_range*static_cast<float>(q)/255.0f-delta_range;
}

template<std::uint32_t DELTA_RANGE>struct DeltaMarshalerF32{
  static std::uint8_t quantized(float value){return quantize_with_range(value,static_cast<float>(DELTA_RANGE));}
  static float unquantized(std::uint8_t value){return unquantize_with_range(value,static_cast<float>(DELTA_RANGE));}
  static void marshal(float value,WriteBuffer&w){w.write_u8(quantized(value));}
  static float unmarshal(ReadBuffer&r){return unquantized(r.read_u8());}
};

class DeltaCompressedCounterHandler{
public:
  ReplicatedFieldHandler<std::uint16_t,IntegerOmitLowerByteMarshaler> absolute_portion;
  struct RelativeCodec{
    static void marshal(std::uint16_t value,WriteBuffer&w){DeltaIntegerMarshaler::marshal(value,w);}
    static std::uint16_t unmarshal(ReadBuffer&r){return DeltaIntegerMarshaler::unmarshal_u16(r);}
  };
  ReplicatedFieldHandler<std::uint16_t,RelativeCodec> relative_portion;
  [[nodiscard]]bool is_absolute_valid()const{return absolute_portion.is_field_valid();}
  [[nodiscard]]bool is_within_delta(std::uint16_t value)const{
    const auto abs=absolute_portion.value().value_or(0);
    return abs<=value&&static_cast<std::uint16_t>(value-abs)<=255;
  }
  void set_value(std::uint16_t value){
    if(!is_absolute_valid()||!is_within_delta(value)){
      const auto rel=static_cast<std::uint16_t>(value%256);
      absolute_portion.set_value(static_cast<std::uint16_t>(value-rel));
      relative_portion.set_value(rel);
    }else{
      relative_portion.set_value(static_cast<std::uint16_t>(value-absolute_portion.value().value_or(0)));
    }
  }
  [[nodiscard]]std::uint16_t value()const{return static_cast<std::uint16_t>(absolute_portion.value().value_or(0)+relative_portion.value().value_or(0));}
};

struct Float16Marshaler{
  float min{},range{};
  Float16Marshaler(float minimum,float maximum):min(minimum),range(maximum-minimum){}
  void marshal(float value,WriteBuffer&w)const{
    const auto normalized=std::clamp((value-min)/range,0.0f,1.0f);
    const auto q=static_cast<std::uint16_t>(normalized*65535.0f);
    Marshaler<std::uint16_t>::marshal(q,w);
  }
  float unmarshal(ReadBuffer&r)const{
    const auto q=Marshaler<std::uint16_t>::unmarshal(r);
    return std::clamp(min+(static_cast<float>(q)/65535.0f)*range,min,min+range);
  }
};

struct Vec2CompMarshaler{
  static void marshal(const Vec2&v,WriteBuffer&w){HalfF32Marshaler::marshal(v.x,w);HalfF32Marshaler::marshal(v.y,w);}
  static Vec2 unmarshal(ReadBuffer&r){return{HalfF32Marshaler::unmarshal(r),HalfF32Marshaler::unmarshal(r)};}
};
struct Vec3CompMarshaler{
  static void marshal(const Vec3&v,WriteBuffer&w){HalfF32Marshaler::marshal(v.x,w);HalfF32Marshaler::marshal(v.y,w);HalfF32Marshaler::marshal(v.z,w);}
  static Vec3 unmarshal(ReadBuffer&r){return{HalfF32Marshaler::unmarshal(r),HalfF32Marshaler::unmarshal(r),HalfF32Marshaler::unmarshal(r)};}
};
struct QuatCompMarshaler{
  static void marshal(const Quat&q,WriteBuffer&w){HalfF32Marshaler::marshal(q.x,w);HalfF32Marshaler::marshal(q.y,w);HalfF32Marshaler::marshal(q.z,w);HalfF32Marshaler::marshal(q.w,w);}
  static Quat unmarshal(ReadBuffer&r){return{HalfF32Marshaler::unmarshal(r),HalfF32Marshaler::unmarshal(r),HalfF32Marshaler::unmarshal(r),HalfF32Marshaler::unmarshal(r)};}
};

struct NonUniformScaleCompMarshaler{
  static void marshal(const Vec3&v,WriteBuffer&w){
    if(v.x==1.0f&&v.y==1.0f&&v.z==1.0f){w.write_u8(0);return;}
    if(std::bit_cast<std::uint32_t>(v.x)==std::bit_cast<std::uint32_t>(v.y)&&std::bit_cast<std::uint32_t>(v.y)==std::bit_cast<std::uint32_t>(v.z)){
      w.write_u8(1);HalfF32Marshaler::marshal(v.x,w);return;
    }
    w.write_u8(2);Vec3CompMarshaler::marshal(v,w);
  }
  static Vec3 unmarshal(ReadBuffer&r){
    switch(r.read_u8()){
      case 0:return{1.0f,1.0f,1.0f};
      case 1:{auto v=HalfF32Marshaler::unmarshal(r);return{v,v,v};}
      default:return Vec3CompMarshaler::unmarshal(r);
    }
  }
};

class PackedSize{
public:
  PackedSize()=default;
  PackedSize(std::uint32_t bytes,std::uint8_t additional_bits):bytes_(bytes+additional_bits/8),additional_bits_(additional_bits%8){}
  static PackedSize from_bits(std::uint32_t bits){PackedSize p;p.bytes_=bits/8;p.additional_bits_=static_cast<std::uint8_t>(bits%8);return p;}
  static PackedSize from_bytes(std::uint32_t bytes){return PackedSize(bytes,0);}
  [[nodiscard]]std::uint32_t total_size_in_bits()const{return bytes_*8+additional_bits_;}
  [[nodiscard]]std::uint32_t bytes()const{return bytes_;}
  [[nodiscard]]std::uint8_t additional_bits()const{return additional_bits_;}
  bool operator==(const PackedSize&)const=default;
private:
  std::uint32_t bytes_{};
  std::uint8_t additional_bits_{};
};
template<>struct Marshaler<PackedSize>{
  static void marshal(const PackedSize&v,WriteBuffer&w){marshal_vlq_u32(w,v.total_size_in_bits());}
  static PackedSize unmarshal(ReadBuffer&r){return PackedSize::from_bits(unmarshal_vlq_u32(r));}
};

template<std::int32_t MIN,std::int32_t MAX>struct IntegerQuantizationMarshalerU8{
  static void marshal(std::int32_t value,WriteBuffer&w){
    const float scale=std::clamp(static_cast<float>(value-MIN)/static_cast<float>(MAX-MIN),0.0f,1.0f);
    w.write_u8(static_cast<std::uint8_t>(scale*255.0f));
  }
  static std::int32_t unmarshal(ReadBuffer&r){
    const auto q=r.read_u8();return MIN+static_cast<std::int32_t>(static_cast<float>(q)*(static_cast<float>(MAX-MIN)/255.0f));
  }
};
template<std::int32_t MIN,std::int32_t MAX>struct IntegerQuantizationMarshalerU16{
  static void marshal(std::int32_t value,WriteBuffer&w){
    const float scale=std::clamp(static_cast<float>(value-MIN)/static_cast<float>(MAX-MIN),0.0f,1.0f);
    Marshaler<std::uint16_t>::marshal(static_cast<std::uint16_t>(scale*65535.0f),w);
  }
  static std::int32_t unmarshal(ReadBuffer&r){
    const auto q=Marshaler<std::uint16_t>::unmarshal(r);return MIN+static_cast<std::int32_t>(static_cast<float>(q)*(static_cast<float>(MAX-MIN)/65535.0f));
  }
};
template<std::int32_t MIN,std::int32_t MAX>struct IntegerQuantizationMarshalerU32{
  static void marshal(std::int32_t value,WriteBuffer&w){
    const long double scale=std::clamp(static_cast<long double>(value-MIN)/static_cast<long double>(MAX-MIN),0.0L,1.0L);
    Marshaler<std::uint32_t>::marshal(static_cast<std::uint32_t>(scale*static_cast<long double>(UINT32_MAX)),w);
  }
  static std::int32_t unmarshal(ReadBuffer&r){
    const auto q=Marshaler<std::uint32_t>::unmarshal(r);return MIN+static_cast<std::int32_t>(static_cast<long double>(q)*(static_cast<long double>(MAX-MIN)/static_cast<long double>(UINT32_MAX)));
  }
};

} // namespace nw::network
