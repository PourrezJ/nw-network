#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace nw::network {

enum class ErrorCode { buffer_underrun, invalid_bool, invalid_range, container_overflow, unknown_type_index };
class ProtocolError : public std::runtime_error {
public:
  ProtocolError(ErrorCode c, std::string m) : std::runtime_error(std::move(m)), code_(c) {}
  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
private:
  ErrorCode code_;
};

enum class Endian { little, big };
inline constexpr Endian CARRIER_ENDIAN = Endian::big;

template<class U> constexpr U byte_swap(U v) noexcept {
  if constexpr(sizeof(U)==2) return static_cast<U>((v>>8)|(v<<8));
  if constexpr(sizeof(U)==4) return static_cast<U>(((v&0x000000ffu)<<24)|((v&0x0000ff00u)<<8)|((v&0x00ff0000u)>>8)|((v&0xff000000u)>>24));
  if constexpr(sizeof(U)==8) {
    std::uint64_t x=static_cast<std::uint64_t>(v);
    x=((x&0x00000000ffffffffull)<<32)|((x&0xffffffff00000000ull)>>32);
    x=((x&0x0000ffff0000ffffull)<<16)|((x&0xffff0000ffff0000ull)>>16);
    x=((x&0x00ff00ff00ff00ffull)<<8)|((x&0xff00ff00ff00ff00ull)>>8);
    return static_cast<U>(x);
  }
  return v;
}

class WriteBuffer {
public:
  explicit WriteBuffer(Endian e=CARRIER_ENDIAN):endian_(e){}
  [[nodiscard]] Endian endian() const noexcept{return endian_;}
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept{return data_;}
  [[nodiscard]] std::span<const std::uint8_t> span() const noexcept{return data_;}
  [[nodiscard]] std::size_t size() const noexcept{return data_.size();}
  void write_u8(std::uint8_t v){data_.push_back(v);}
  void write_bytes(std::span<const std::uint8_t> s){data_.insert(data_.end(),s.begin(),s.end());}
  template<class T> requires(std::is_integral_v<T> && sizeof(T)>1)
  void write_int(T v){
    using U=std::make_unsigned_t<T>;
    U raw=static_cast<U>(v);
    if((endian_==Endian::big)==(std::endian::native==std::endian::little)) raw=byte_swap(raw);
    const auto* p=reinterpret_cast<const std::uint8_t*>(&raw);
    write_bytes({p,sizeof(U)});
  }
private:
  Endian endian_;
  std::vector<std::uint8_t> data_;
};

class ReadBuffer {
public:
  explicit ReadBuffer(std::span<const std::uint8_t> s,Endian e=CARRIER_ENDIAN):data_(s),endian_(e){}
  [[nodiscard]] Endian endian()const noexcept{return endian_;}
  [[nodiscard]] std::size_t position()const noexcept{return pos_;}
  [[nodiscard]] std::size_t left()const noexcept{return data_.size()-pos_;}
  [[nodiscard]] bool empty()const noexcept{return pos_==data_.size();}
  std::uint8_t read_u8(){require(1);return data_[pos_++];}
  std::span<const std::uint8_t> read_bytes(std::size_t n){require(n);auto s=data_.subspan(pos_,n);pos_+=n;return s;}
  template<std::size_t N> std::array<std::uint8_t,N> read_array(){auto s=read_bytes(N);std::array<std::uint8_t,N>a{};std::copy(s.begin(),s.end(),a.begin());return a;}
  template<class T> requires(std::is_integral_v<T> && sizeof(T)>1)
  T read_int(){
    using U=std::make_unsigned_t<T>;
    auto s=read_bytes(sizeof(U));
    U raw{};
    std::memcpy(&raw,s.data(),sizeof(U));
    if((endian_==Endian::big)==(std::endian::native==std::endian::little)) raw=byte_swap(raw);
    return static_cast<T>(raw);
  }
private:
  void require(std::size_t n)const{if(n>left())throw ProtocolError(ErrorCode::buffer_underrun,"buffer underrun");}
  std::span<const std::uint8_t> data_;
  Endian endian_;
  std::size_t pos_{};
};

inline void marshal_vlq_u32(WriteBuffer& w,std::uint32_t v){
  if(v<0x80)w.write_u8(static_cast<std::uint8_t>(v));
  else if(v<0x4000){w.write_u8(static_cast<std::uint8_t>(0x80|(v&0x3f)));w.write_u8(static_cast<std::uint8_t>((v&0x3fc0)>>6));}
  else if(v<0x20'0000){w.write_u8(static_cast<std::uint8_t>(0xc0|(v&0x1f)));w.write_u8(static_cast<std::uint8_t>((v&0x1fe0)>>5));w.write_u8(static_cast<std::uint8_t>((v&0x1fe000)>>13));}
  else if(v<0x1000'0000){w.write_u8(static_cast<std::uint8_t>(0xe0|(v&0x0f)));w.write_u8(static_cast<std::uint8_t>((v&0xff0)>>4));w.write_u8(static_cast<std::uint8_t>((v&0xff000)>>12));w.write_u8(static_cast<std::uint8_t>((v&0xff00000)>>20));}
  else {w.write_u8(static_cast<std::uint8_t>(0xf0|(v&7)));w.write_u8(static_cast<std::uint8_t>((v&0x7f8)>>3));w.write_u8(static_cast<std::uint8_t>((v&0x7f800)>>11));w.write_u8(static_cast<std::uint8_t>((v&0x7f80000)>>19));w.write_u8(static_cast<std::uint8_t>((v&0xf8000000)>>27));}
}
inline std::uint32_t unmarshal_vlq_u32(ReadBuffer& r){
  auto f=r.read_u8();
  if(f<0x80)return f;
  if(f<0xc0)return(f&~0xc0u)|(static_cast<std::uint32_t>(r.read_u8())<<6);
  if(f<0xe0){auto a=r.read_u8(),b=r.read_u8();return(f&~0xe0u)|(static_cast<std::uint32_t>(a)<<5)|(static_cast<std::uint32_t>(b)<<13);}
  if(f<0xf0){auto a=r.read_u8(),b=r.read_u8(),c=r.read_u8();return(f&~0xf0u)|(static_cast<std::uint32_t>(a)<<4)|(static_cast<std::uint32_t>(b)<<12)|(static_cast<std::uint32_t>(c)<<20);}
  auto a=r.read_u8(),b=r.read_u8(),c=r.read_u8(),d=r.read_u8();
  return(f&~0xf8u)|(static_cast<std::uint32_t>(a)<<3)|(static_cast<std::uint32_t>(b)<<11)|(static_cast<std::uint32_t>(c)<<19)|(static_cast<std::uint32_t>(d)<<27);
}
inline void marshal_vlq_u16(WriteBuffer&w,std::uint16_t v){marshal_vlq_u32(w,v);}
inline std::uint16_t unmarshal_vlq_u16(ReadBuffer&r){
  auto v=unmarshal_vlq_u32(r);
  if(v>0xffff)throw ProtocolError(ErrorCode::container_overflow,"VLQ u16 overflow");
  return static_cast<std::uint16_t>(v);
}
inline void marshal_vlq_u64(WriteBuffer&w,std::uint64_t v){
  auto b=[&](unsigned s){return static_cast<std::uint8_t>((v>>s)&0xff);};
  if(v<0x80)w.write_u8(b(0));
  else if(v<0x4000){w.write_u8(static_cast<std::uint8_t>(0x80|(v&0x3f)));w.write_u8(b(6));}
  else if(v<0x20'0000){w.write_u8(static_cast<std::uint8_t>(0xc0|(v&0x1f)));w.write_u8(b(5));w.write_u8(b(13));}
  else if(v<0x1000'0000){w.write_u8(static_cast<std::uint8_t>(0xe0|(v&0x0f)));w.write_u8(b(4));w.write_u8(b(12));w.write_u8(b(20));}
  else if(v<0x0000'0008'0000'0000ull){w.write_u8(static_cast<std::uint8_t>(0xf0|(v&7)));w.write_u8(b(3));w.write_u8(b(11));w.write_u8(b(19));w.write_u8(b(27));}
  else if(v<0x0000'0400'0000'0000ull){w.write_u8(static_cast<std::uint8_t>(0xf8|(v&3)));w.write_u8(b(2));w.write_u8(b(10));w.write_u8(b(18));w.write_u8(b(26));w.write_u8(b(34));}
  else if(v<0x0002'0000'0000'0000ull){w.write_u8(static_cast<std::uint8_t>(0xfc|(v&1)));w.write_u8(b(1));w.write_u8(b(9));w.write_u8(b(17));w.write_u8(b(25));w.write_u8(b(33));w.write_u8(b(41));}
  else if(v<0x0100'0000'0000'0000ull){w.write_u8(0xfe);for(unsigned s=0;s<=48;s+=8)w.write_u8(b(s));}
  else {w.write_u8(0xff);for(unsigned s=0;s<=56;s+=8)w.write_u8(b(s));}
}
inline std::uint64_t unmarshal_vlq_u64(ReadBuffer&r){
  auto f=r.read_u8();
  if(f<0x80)return f;
  auto x=[&](){return static_cast<std::uint64_t>(r.read_u8());};
  if(f<0xc0)return(f&~0xc0u)|(x()<<6);
  if(f<0xe0){auto a=x(),b=x();return(f&~0xe0u)|(a<<5)|(b<<13);}
  if(f<0xf0){auto a=x(),b=x(),c=x();return(f&~0xf0u)|(a<<4)|(b<<12)|(c<<20);}
  if(f<0xf8){auto a=x(),b=x(),c=x(),d=x();return(f&~0xf8u)|(a<<3)|(b<<11)|(c<<19)|(d<<27);}
  if(f<0xfc){auto a=x(),b=x(),c=x(),d=x(),e=x();return(f&~0xfcu)|(a<<2)|(b<<10)|(c<<18)|(d<<26)|(e<<34);}
  if(f<0xfe){auto a=x(),b=x(),c=x(),d=x(),e=x(),g=x();return(f&~0xfeu)|(a<<1)|(b<<9)|(c<<17)|(d<<25)|(e<<33)|(g<<41);}
  std::uint64_t out=0;int n=f==0xfe?7:8;for(int i=0;i<n;++i)out|=x()<<(8*i);return out;
}

struct VlqU16{std::uint16_t value{};auto operator<=>(const VlqU16&)const=default;};
struct VlqU32{std::uint32_t value{};auto operator<=>(const VlqU32&)const=default;};
struct VlqU64{std::uint64_t value{};auto operator<=>(const VlqU64&)const=default;};

struct Uuid{
  std::array<std::uint8_t,16>bytes{};
  static constexpr Uuid nil(){return{};}
  static constexpr Uuid from_bytes(std::array<std::uint8_t,16>v){return{v};}
  constexpr bool is_nil()const{for(auto b:bytes)if(b)return false;return true;}
  auto operator<=>(const Uuid&)const=default;
};
struct Crc32{
  std::uint32_t value{};
  constexpr explicit Crc32(std::uint32_t v=0):value(v){}
  static constexpr std::uint32_t compute(std::span<const std::uint8_t>s){std::uint32_t c=0xffffffffu;for(auto b:s){c^=b;for(int i=0;i<8;++i)c=(c&1)?0xedb88320u^(c>>1):c>>1;}return c^0xffffffffu;}
  auto operator<=>(const Crc32&)const=default;
};
struct ActorRequestId{std::uint64_t target_local_id{0xffffffffull};std::uint64_t source_actor_ref{};auto operator<=>(const ActorRequestId&)const=default;};
template<class Tag>struct OpaqueU64{std::uint64_t value{};constexpr explicit OpaqueU64(std::uint64_t v=0):value(v){};auto operator<=>(const OpaqueU64&)const=default;};
struct EntityIdTag{};struct ComponentIdTag{};struct GdeIdTag{};
using EntityId=OpaqueU64<EntityIdTag>;
using ComponentId=OpaqueU64<ComponentIdTag>;
using GdeId=OpaqueU64<GdeIdTag>;
struct AssetId{Uuid guid{};std::uint32_t sub_id{};auto operator<=>(const AssetId&)const=default;};
struct GdeRef{Uuid value{};auto operator<=>(const GdeRef&)const=default;};
struct TimePoint{std::uint64_t nanoseconds_since_server_start{};auto operator<=>(const TimePoint&)const=default;};
enum class ReplicationCategory:std::uint8_t{Uncategorized=0,PlayerCharacter=1,NonPlayerCharacter=2,ImportantNonPlayerCharacter=3,Buildable=6};

class SequenceNumber{
public:
  constexpr explicit SequenceNumber(std::uint64_t r=0):raw_(r){}
  static constexpr SequenceNumber invalid(){return SequenceNumber(0);}
  static constexpr SequenceNumber valid_non_sequence(){return SequenceNumber(1);}
  static constexpr SequenceNumber seq(std::uint64_t v){return SequenceNumber(v+1);}
  constexpr bool is_valid()const{return raw_!=0;}
  constexpr std::uint64_t raw()const{return raw_;}
  auto operator<=>(const SequenceNumber&)const=default;
private:
  std::uint64_t raw_{};
};

inline constexpr std::size_t WIRE_VEC_CAP=0x0200'0000;
template<class T,class E=void>struct Marshaler;
template<class T>requires(std::is_integral_v<T>&&!std::is_same_v<T,bool>)
struct Marshaler<T>{
  static void marshal(T v,WriteBuffer&w){if constexpr(sizeof(T)==1)w.write_u8(static_cast<std::uint8_t>(v));else w.write_int(v);}
  static T unmarshal(ReadBuffer&r){if constexpr(sizeof(T)==1)return static_cast<T>(r.read_u8());else return r.read_int<T>();}
};
template<>struct Marshaler<bool>{static void marshal(bool v,WriteBuffer&w){w.write_u8(v?1:0);}static bool unmarshal(ReadBuffer&r){auto v=r.read_u8();if(v>1)throw ProtocolError(ErrorCode::invalid_bool,"invalid strict bool");return v!=0;}};
template<>struct Marshaler<float>{static void marshal(float v,WriteBuffer&w){Marshaler<std::uint32_t>::marshal(std::bit_cast<std::uint32_t>(v),w);}static float unmarshal(ReadBuffer&r){return std::bit_cast<float>(Marshaler<std::uint32_t>::unmarshal(r));}};
template<>struct Marshaler<double>{static void marshal(double v,WriteBuffer&w){Marshaler<std::uint64_t>::marshal(std::bit_cast<std::uint64_t>(v),w);}static double unmarshal(ReadBuffer&r){return std::bit_cast<double>(Marshaler<std::uint64_t>::unmarshal(r));}};
template<>struct Marshaler<Uuid>{static void marshal(const Uuid&v,WriteBuffer&w){w.write_bytes(v.bytes);}static Uuid unmarshal(ReadBuffer&r){return Uuid::from_bytes(r.read_array<16>());}};
template<>struct Marshaler<Crc32>{static void marshal(Crc32 v,WriteBuffer&w){Marshaler<std::uint32_t>::marshal(v.value,w);}static Crc32 unmarshal(ReadBuffer&r){return Crc32(Marshaler<std::uint32_t>::unmarshal(r));}};
template<class Tag>struct Marshaler<OpaqueU64<Tag>>{static void marshal(OpaqueU64<Tag>v,WriteBuffer&w){Marshaler<std::uint64_t>::marshal(v.value,w);}static OpaqueU64<Tag>unmarshal(ReadBuffer&r){return OpaqueU64<Tag>(Marshaler<std::uint64_t>::unmarshal(r));}};
template<>struct Marshaler<ActorRequestId>{static void marshal(const ActorRequestId&v,WriteBuffer&w){Marshaler<std::uint64_t>::marshal(v.target_local_id,w);Marshaler<std::uint64_t>::marshal(v.source_actor_ref,w);}static ActorRequestId unmarshal(ReadBuffer&r){auto a=Marshaler<std::uint64_t>::unmarshal(r);auto b=Marshaler<std::uint64_t>::unmarshal(r);return{a,b};}};
template<>struct Marshaler<SequenceNumber>{static void marshal(SequenceNumber v,WriteBuffer&w){marshal_vlq_u64(w,v.raw());}static SequenceNumber unmarshal(ReadBuffer&r){return SequenceNumber(unmarshal_vlq_u64(r));}};
template<>struct Marshaler<VlqU64>{static void marshal(VlqU64 v,WriteBuffer&w){marshal_vlq_u64(w,v.value);}static VlqU64 unmarshal(ReadBuffer&r){return{unmarshal_vlq_u64(r)};}};
template<>struct Marshaler<std::string>{
  static void marshal(const std::string&v,WriteBuffer&w){if(v.size()>WIRE_VEC_CAP)throw ProtocolError(ErrorCode::container_overflow,"string overflow");marshal_vlq_u32(w,static_cast<std::uint32_t>(v.size()));w.write_bytes({reinterpret_cast<const std::uint8_t*>(v.data()),v.size()});}
  static std::string unmarshal(ReadBuffer&r){auto n=unmarshal_vlq_u32(r);if(n>WIRE_VEC_CAP)throw ProtocolError(ErrorCode::container_overflow,"string overflow");auto s=r.read_bytes(n);return{reinterpret_cast<const char*>(s.data()),s.size()};}
};
template<class T>struct Marshaler<std::vector<T>>{
  static void marshal(const std::vector<T>&v,WriteBuffer&w){if(v.size()>WIRE_VEC_CAP)throw ProtocolError(ErrorCode::container_overflow,"vector overflow");marshal_vlq_u32(w,static_cast<std::uint32_t>(v.size()));for(const auto&x:v)Marshaler<T>::marshal(x,w);}
  static std::vector<T>unmarshal(ReadBuffer&r){auto n=unmarshal_vlq_u32(r);if(n>WIRE_VEC_CAP)throw ProtocolError(ErrorCode::container_overflow,"vector overflow");std::vector<T>v;v.reserve(n);for(std::uint32_t i=0;i<n;++i)v.push_back(Marshaler<T>::unmarshal(r));return v;}
};


struct Vec3{float x{},y{},z{};bool operator==(const Vec3&)const=default;};
template<>struct Marshaler<Vec3>{
  static void marshal(const Vec3&v,WriteBuffer&w){Marshaler<float>::marshal(v.x,w);Marshaler<float>::marshal(v.y,w);Marshaler<float>::marshal(v.z,w);}
  static Vec3 unmarshal(ReadBuffer&r){return{Marshaler<float>::unmarshal(r),Marshaler<float>::unmarshal(r),Marshaler<float>::unmarshal(r)};}
};

struct HomePointPersistentRef{Uuid gde_id{};std::uint64_t home_point_unique_id_value{};std::uint64_t gde_id_hash{};bool operator==(const HomePointPersistentRef&)const=default;};
template<>struct Marshaler<HomePointPersistentRef>{
  static void marshal(const HomePointPersistentRef&v,WriteBuffer&w){Marshaler<Uuid>::marshal(v.gde_id,w);Marshaler<std::uint64_t>::marshal(v.home_point_unique_id_value,w);Marshaler<std::uint64_t>::marshal(v.gde_id_hash,w);}
  static HomePointPersistentRef unmarshal(ReadBuffer&r){auto a=Marshaler<Uuid>::unmarshal(r);auto b=Marshaler<std::uint64_t>::unmarshal(r);auto c=Marshaler<std::uint64_t>::unmarshal(r);return{a,b,c};}
};
struct HomePointReplicatedState{
  HomePointPersistentRef persistent_ref{};std::string name;Vec3 position{};std::uint64_t cooldown_duration_ns{};std::uint64_t cooldown_end_ns{};std::uint32_t respawn_type{};bool is_overloaded{};std::uint8_t is_hidden_from_respawn{};std::string home_point_unique_id;std::uint32_t respawn_modifier{};
  bool operator==(const HomePointReplicatedState&)const=default;
};
template<>struct Marshaler<HomePointReplicatedState>{
  static void marshal(const HomePointReplicatedState&v,WriteBuffer&w){Marshaler<HomePointPersistentRef>::marshal(v.persistent_ref,w);Marshaler<std::string>::marshal(v.name,w);Marshaler<Vec3>::marshal(v.position,w);Marshaler<std::uint64_t>::marshal(v.cooldown_duration_ns,w);Marshaler<std::uint64_t>::marshal(v.cooldown_end_ns,w);Marshaler<std::uint32_t>::marshal(v.respawn_type,w);Marshaler<bool>::marshal(v.is_overloaded,w);Marshaler<std::uint8_t>::marshal(v.is_hidden_from_respawn,w);Marshaler<std::string>::marshal(v.home_point_unique_id,w);Marshaler<std::uint32_t>::marshal(v.respawn_modifier,w);}
  static HomePointReplicatedState unmarshal(ReadBuffer&r){HomePointReplicatedState v;v.persistent_ref=Marshaler<HomePointPersistentRef>::unmarshal(r);v.name=Marshaler<std::string>::unmarshal(r);v.position=Marshaler<Vec3>::unmarshal(r);v.cooldown_duration_ns=Marshaler<std::uint64_t>::unmarshal(r);v.cooldown_end_ns=Marshaler<std::uint64_t>::unmarshal(r);v.respawn_type=Marshaler<std::uint32_t>::unmarshal(r);v.is_overloaded=Marshaler<bool>::unmarshal(r);v.is_hidden_from_respawn=Marshaler<std::uint8_t>::unmarshal(r);v.home_point_unique_id=Marshaler<std::string>::unmarshal(r);v.respawn_modifier=Marshaler<std::uint32_t>::unmarshal(r);return v;}
};

struct ObjectiveResponseParametersReplicatedState{
  Uuid objective_uuid{};std::uint64_t response_time{};std::uint16_t response_id{};bool is_selected{};bool is_complete{};bool is_repeatable{};bool has_target{};std::uint64_t target_id{};bool has_response_values{};std::vector<std::uint32_t>response_values;
  bool operator==(const ObjectiveResponseParametersReplicatedState&)const=default;
};
template<>struct Marshaler<ObjectiveResponseParametersReplicatedState>{
  static void marshal(const ObjectiveResponseParametersReplicatedState&v,WriteBuffer&w){if(v.response_values.size()>7)throw ProtocolError(ErrorCode::container_overflow,"objective response values > 7");Marshaler<Uuid>::marshal(v.objective_uuid,w);Marshaler<std::uint64_t>::marshal(v.response_time,w);Marshaler<std::uint16_t>::marshal(v.response_id,w);Marshaler<bool>::marshal(v.is_selected,w);Marshaler<bool>::marshal(v.is_complete,w);Marshaler<bool>::marshal(v.is_repeatable,w);Marshaler<bool>::marshal(v.has_target,w);Marshaler<std::uint64_t>::marshal(v.target_id,w);Marshaler<bool>::marshal(v.has_response_values,w);marshal_vlq_u32(w,static_cast<std::uint32_t>(v.response_values.size()));for(auto x:v.response_values)Marshaler<std::uint32_t>::marshal(x,w);}
  static ObjectiveResponseParametersReplicatedState unmarshal(ReadBuffer&r){ObjectiveResponseParametersReplicatedState v;v.objective_uuid=Marshaler<Uuid>::unmarshal(r);v.response_time=Marshaler<std::uint64_t>::unmarshal(r);v.response_id=Marshaler<std::uint16_t>::unmarshal(r);v.is_selected=Marshaler<bool>::unmarshal(r);v.is_complete=Marshaler<bool>::unmarshal(r);v.is_repeatable=Marshaler<bool>::unmarshal(r);v.has_target=Marshaler<bool>::unmarshal(r);v.target_id=Marshaler<std::uint64_t>::unmarshal(r);v.has_response_values=Marshaler<bool>::unmarshal(r);auto n=unmarshal_vlq_u32(r);if(n>7)throw ProtocolError(ErrorCode::container_overflow,"objective response values > 7");v.response_values.reserve(n);for(std::uint32_t i=0;i<n;++i)v.response_values.push_back(Marshaler<std::uint32_t>::unmarshal(r));return v;}
};

struct WarScheduleAdjustmentReplicatedState{Uuid field_10_id{};std::uint32_t field_20{};std::uint16_t field_24{};std::uint32_t field_28{};std::uint64_t field_38{};bool operator==(const WarScheduleAdjustmentReplicatedState&)const=default;};
template<>struct Marshaler<WarScheduleAdjustmentReplicatedState>{
  static void marshal(const WarScheduleAdjustmentReplicatedState&v,WriteBuffer&w){Marshaler<Uuid>::marshal(v.field_10_id,w);Marshaler<std::uint32_t>::marshal(v.field_20,w);Marshaler<std::uint16_t>::marshal(v.field_24,w);Marshaler<std::uint32_t>::marshal(v.field_28,w);Marshaler<std::uint64_t>::marshal(v.field_38,w);}
  static WarScheduleAdjustmentReplicatedState unmarshal(ReadBuffer&r){auto a=Marshaler<Uuid>::unmarshal(r);auto b=Marshaler<std::uint32_t>::unmarshal(r);auto c=Marshaler<std::uint16_t>::unmarshal(r);auto d=Marshaler<std::uint32_t>::unmarshal(r);auto e=Marshaler<std::uint64_t>::unmarshal(r);return{a,b,c,d,e};}
};

template<class T>class ReplicatedField{
public:
  const std::optional<T>&value()const{return value_;}
  bool has_value()const{return value_.has_value();}
  SequenceNumber last_modified()const{return last_modified_;}
  void set_value(T v){value_=std::move(v);last_modified_=SequenceNumber::valid_non_sequence();}
  void set_last_modified(SequenceNumber s){last_modified_=s;}
  bool is_dirty(SequenceNumber baseline)const{return baseline<last_modified_;}
  bool has_new_network_data()const{return new_network_data_;}
  void reset_has_new_network_data(){new_network_data_=false;}
  void marshal(WriteBuffer&w)const{if(!value_)throw ProtocolError(ErrorCode::invalid_range,"empty replicated field");Marshaler<T>::marshal(*value_,w);}
  void unmarshal(ReadBuffer&r){value_=Marshaler<T>::unmarshal(r);last_modified_=SequenceNumber::valid_non_sequence();new_network_data_=true;}
private:
  std::optional<T>value_;
  SequenceNumber last_modified_{SequenceNumber::invalid()};
  bool new_network_data_{};
};
template<class T>using ReplicatedFieldHandler=ReplicatedField<T>;

inline constexpr std::size_t REPLICATED_CONTAINER_FIXED_JOURNAL_SIZE=10;
enum class ChangeOp{add,update,remove};
template<class K,class V>struct Change{
  ChangeOp op{ChangeOp::remove};K key{};std::optional<V>value{};SequenceNumber sequence{SequenceNumber::invalid()};
  static Change update(K k,V v,SequenceNumber s){return{ChangeOp::update,std::move(k),std::move(v),s};}
  static Change remove(K k,SequenceNumber s){return{ChangeOp::remove,std::move(k),std::nullopt,s};}
  bool live()const{return op!=ChangeOp::remove;}
};
template<class K,class V>struct ChangeSet{SequenceNumber sequence{SequenceNumber::invalid()};std::vector<Change<K,V>>changes;};

template<class C>struct ContainerTraits;
template<class T>struct ContainerTraits<std::vector<T>>{
  using key_type=VlqU64;using value_type=T;
  static void marshal_entries(const std::vector<T>&c,WriteBuffer&w){for(const auto&v:c)Marshaler<T>::marshal(v,w);}
  static std::vector<T>unmarshal_entries(ReadBuffer&r,std::size_t n){std::vector<T>c;c.reserve(n);for(std::size_t i=0;i<n;++i)c.push_back(Marshaler<T>::unmarshal(r));return c;}
  static std::optional<T>lookup(const std::vector<T>&c,const VlqU64&k){auto i=static_cast<std::size_t>(k.value);if(i<c.size())return c[i];return std::nullopt;}
};

template<class C,std::size_t CAP=WIRE_VEC_CAP>class ReplicatedContainer{
public:
  using traits=ContainerTraits<C>;using key_type=typename traits::key_type;using value_type=typename traits::value_type;
  static ReplicatedContainer snapshot(SequenceNumber s,C v){ReplicatedContainer r;r.last_modified_=s;r.values_=std::move(v);return r;}
  static ReplicatedContainer delta(std::vector<Change<key_type,value_type>>c){ReplicatedContainer r;r.initialize_=false;r.current_changes_=std::move(c);r.last_modified_=SequenceNumber::valid_non_sequence();return r;}
  const C&values()const{return values_;}
  const auto&current_changes()const{return current_changes_;}
  void marshal(WriteBuffer&w)const{
    if(current_changes_.empty()){
      marshal_vlq_u32(w,0);
      Marshaler<SequenceNumber>::marshal(last_modified_,w);
      marshal_vlq_u32(w,static_cast<std::uint32_t>(values_.size()));
      traits::marshal_entries(values_,w);
      return;
    }
    marshal_vlq_u32(w,static_cast<std::uint32_t>(current_changes_.size()));
    std::size_t at=0;SequenceNumber previous=SequenceNumber::invalid();
    while(at<current_changes_.size()){
      auto n=std::min<std::size_t>(7,current_changes_.size()-at);
      std::uint8_t mask=0;
      for(std::size_t i=0;i<n;++i)if(current_changes_[at+i].live())mask|=static_cast<std::uint8_t>(1u<<i);
      w.write_u8(mask);
      for(std::size_t i=0;i<n;++i){
        const auto&ch=current_changes_[at+i];
        Marshaler<key_type>::marshal(ch.key,w);
        auto s=ch.sequence==previous?SequenceNumber::valid_non_sequence():ch.sequence;
        Marshaler<SequenceNumber>::marshal(s,w);
        if(ch.sequence!=previous)previous=ch.sequence;
        if(ch.live()){
          auto v=ch.value?ch.value:traits::lookup(values_,ch.key);
          Marshaler<value_type>::marshal(v.value_or(value_type{}),w);
        }
      }
      at+=n;
    }
  }
  static ReplicatedContainer unmarshal(ReadBuffer&r){
    auto mode=unmarshal_vlq_u32(r);
    if(mode>CAP)throw ProtocolError(ErrorCode::container_overflow,"replicated container overflow");
    if(mode==0){
      auto seq=Marshaler<SequenceNumber>::unmarshal(r);
      auto n=unmarshal_vlq_u32(r);
      if(n>CAP)throw ProtocolError(ErrorCode::container_overflow,"snapshot overflow");
      ReplicatedContainer out;out.values_=traits::unmarshal_entries(r,n);out.last_modified_=SequenceNumber::valid_non_sequence();out.last_full_=seq;return out;
    }
    ReplicatedContainer out;out.initialize_=false;out.last_modified_=SequenceNumber::valid_non_sequence();
    SequenceNumber previous=SequenceNumber::invalid();std::size_t left=mode;
    while(left){
      auto n=std::min<std::size_t>(7,left);auto mask=r.read_u8();
      for(std::size_t i=0;i<n;++i){
        auto k=Marshaler<key_type>::unmarshal(r);
        auto raw=Marshaler<SequenceNumber>::unmarshal(r);
        auto seq=raw==SequenceNumber::valid_non_sequence()?previous:raw;
        if(raw!=SequenceNumber::valid_non_sequence())previous=raw;
        if(mask&(1u<<i))out.current_changes_.push_back(Change<key_type,value_type>::update(k,Marshaler<value_type>::unmarshal(r),seq));
        else out.current_changes_.push_back(Change<key_type,value_type>::remove(k,seq));
      }
      left-=n;
    }
    return out;
  }
private:
  C values_{};
  std::vector<Change<key_type,value_type>>current_changes_;
  SequenceNumber last_modified_{SequenceNumber::invalid()},last_full_{SequenceNumber::invalid()};
  bool initialize_{true};
};

struct NetworkFieldDescriptor{std::uint32_t index{};std::string_view name{};std::int32_t group{-1};std::string_view rust_type{};std::string_view native_type{};std::string_view wire_shape{};};
struct NetworkTypeDescriptor{Uuid type_id{};std::uint32_t type_index{};std::string_view name{};std::string_view cpp_name{};bool replicated_state{};bool direct_message{};std::uint32_t field_offset{};std::uint32_t field_count{};};
std::span<const NetworkFieldDescriptor> network_fields() noexcept;
std::span<const NetworkTypeDescriptor> network_types() noexcept;
const NetworkTypeDescriptor* type_by_index(std::uint32_t) noexcept;
std::span<const NetworkFieldDescriptor> fields_for(const NetworkTypeDescriptor&) noexcept;
std::span<const std::string_view> exported_state_names() noexcept;
std::span<const std::uint32_t> exported_state_type_indices() noexcept;

class DynamicReplicatedState{
public:
  explicit DynamicReplicatedState(std::uint32_t i):type_index_(i){}
  std::uint32_t type_index()const{return type_index_;}
  void set_encoded(std::string name,std::span<const std::uint8_t>b,SequenceNumber s=SequenceNumber::valid_non_sequence()){
    auto&f=fields_[std::move(name)];f.bytes.assign(b.begin(),b.end());f.seq=s;
  }
  bool marshal_contents(WriteBuffer&w,SequenceNumber baseline=SequenceNumber::invalid())const{
    auto*d=type_by_index(type_index_);if(!d)return false;
    auto ds=fields_for(*d);
    std::map<int,std::vector<const NetworkFieldDescriptor*>>groups;
    for(const auto&fd:ds)groups[std::max(0,fd.group)].push_back(&fd);
    if(groups.empty())return false;
    bool any=false;int maxg=groups.rbegin()->first;
    for(int dc=0;dc<=maxg/8;++dc){
      std::uint8_t dm=0;
      for(int bit=0;bit<8;++bit){
        int g=dc*8+bit;auto gi=groups.find(g);if(gi==groups.end())continue;
        for(auto*fd:gi->second){auto it=fields_.find(std::string(fd->name));if(it!=fields_.end()&&baseline<it->second.seq){dm|=static_cast<std::uint8_t>(1u<<bit);any=true;break;}}
      }
      w.write_u8(dm);
      for(int bit=0;bit<8;++bit){
        int g=dc*8+bit;if(!(dm&(1u<<bit)))continue;auto&v=groups.at(g);
        for(std::size_t start=0;start<v.size();start+=7){
          std::uint8_t fm=0;bool later=false;
          for(std::size_t i=0;i<7&&start+i<v.size();++i){auto it=fields_.find(std::string(v[start+i]->name));if(it!=fields_.end()&&baseline<it->second.seq)fm|=static_cast<std::uint8_t>(1u<<i);}
          for(std::size_t j=start+7;j<v.size();++j){auto it=fields_.find(std::string(v[j]->name));if(it!=fields_.end()&&baseline<it->second.seq){later=true;break;}}
          if(later)fm|=0x80;
          if(start==0||fm){
            w.write_u8(fm);
            for(std::size_t i=0;i<7&&start+i<v.size();++i)if(fm&(1u<<i))w.write_bytes(fields_.at(std::string(v[start+i]->name)).bytes);
          }
          if(!later)break;
        }
      }
    }
    return any;
  }
private:
  struct F{std::vector<std::uint8_t>bytes;SequenceNumber seq{SequenceNumber::invalid()};};
  std::uint32_t type_index_{};
  std::map<std::string,F,std::less<>>fields_;
};

struct WarboardStatRow{
  std::uint64_t mask{};
  std::vector<std::uint64_t>values;
  std::optional<std::uint64_t>stat(std::uint32_t bit)const{
    if(bit>=64||((mask>>bit)&1)==0)return std::nullopt;
    auto lower=bit?mask&((1ull<<bit)-1):0;
    auto rank=std::popcount(lower);
    if(rank>=values.size())return std::nullopt;
    return values[rank];
  }
};
struct WarboardPlayerStats{std::uint8_t player_index{};WarboardStatRow row;};
struct WarboardStatBlock{std::vector<WarboardPlayerStats>players;};
struct WarboardStats{std::uint16_t counter{};std::uint8_t local_index{};WarboardStatRow local;std::vector<WarboardStatBlock>blocks;};
inline void marshal_warboard_row(const WarboardStatRow&r,WriteBuffer&w){
  if(r.values.size()!=std::popcount(r.mask))throw ProtocolError(ErrorCode::invalid_range,"warboard value count");
  marshal_vlq_u64(w,r.mask);
  for(auto v:r.values)marshal_vlq_u64(w,v);
}
inline WarboardStatRow unmarshal_warboard_row(ReadBuffer&r){
  WarboardStatRow out;out.mask=unmarshal_vlq_u64(r);
  if(out.mask&1)throw ProtocolError(ErrorCode::invalid_range,"warboard bit0");
  auto n=std::popcount(out.mask);
  if(n>r.left())throw ProtocolError(ErrorCode::buffer_underrun,"warboard truncated");
  out.values.reserve(n);for(unsigned i=0;i<n;++i)out.values.push_back(unmarshal_vlq_u64(r));return out;
}
inline void marshal_warboard(const WarboardStats&s,WriteBuffer&w){
  Marshaler<std::uint16_t>::marshal(s.counter,w);Marshaler<std::uint8_t>::marshal(s.local_index,w);marshal_warboard_row(s.local,w);
  for(const auto&b:s.blocks){
    if(b.players.size()>255)throw ProtocolError(ErrorCode::container_overflow,"warboard block");
    w.write_u8(static_cast<std::uint8_t>(b.players.size()));
    for(const auto&p:b.players){w.write_u8(p.player_index);marshal_warboard_row(p.row,w);}
  }
}
inline WarboardStats unmarshal_warboard(ReadBuffer&r){
  WarboardStats s;s.counter=Marshaler<std::uint16_t>::unmarshal(r);s.local_index=Marshaler<std::uint8_t>::unmarshal(r);s.local=unmarshal_warboard_row(r);
  while(!r.empty()){
    WarboardStatBlock b;auto n=r.read_u8();
    if(static_cast<std::size_t>(n)*2>r.left())throw ProtocolError(ErrorCode::buffer_underrun,"warboard block");
    for(unsigned i=0;i<n;++i){auto idx=r.read_u8();b.players.push_back({idx,unmarshal_warboard_row(r)});}
    s.blocks.push_back(std::move(b));
  }
  return s;
}

} // namespace nw::network
