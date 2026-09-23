#include "km/h264.h"
#include "km/timing.h"
#include "km/latest_frame.h"
#include "km/nv12.h"
#include "km/receiver_contracts.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr<<__FILE__<<":"<<__LINE__<<": " #x "\n";return 1;} } while(false)
int main() {
    km::TimestampUnwrapper32 u;
    CHECK(u.unwrap(0xfffffff0u)==4294967280ll);
    CHECK(u.unwrap(0x10u)==4294967312ll);
    CHECK(u.unwrap(0x08u)==4294967304ll); // limited reordering
    u.reset();CHECK(u.unwrap(90000)==90000);
    CHECK(km::TicksToMicroseconds(90000,90000)==1000000);
    CHECK(km::TicksToMicroseconds(-45000,90000)==-500000);
    bool threw=false;try { (void)km::TicksToMicroseconds(1,0); } catch(const std::invalid_argument&) { threw=true; }
    CHECK(threw);
    threw=false;try { (void)km::TicksToMicroseconds(INT64_MAX,1); } catch(const std::overflow_error&) { threw=true; }
    CHECK(threw);
    km::RationalPacer p(30,1,123);
    for (uint64_t i=0;i<=108000;++i) CHECK(p.next()==123+i*1000000000ull/30);
    km::RationalPacer ntsc(30000,1001,0);
    for (uint64_t i=0;i<=30000;++i) CHECK(ntsc.next()==i*1001000000000ull/30000);
    std::vector<uint8_t> annex{0,0,0,1,0x67,0x42,0x80,0,0,1,0x68,0xce,0,0,1,0x65,0xaa};
    std::vector<km::h264::Bytes> nalus;
    CHECK(km::h264::SplitAnnexB(annex,nalus));CHECK(nalus.size()==3);
    std::vector<uint8_t> avcc;
    CHECK(km::h264::AnnexBToLengthPrefixed4(annex,avcc));
    CHECK(km::h264::SplitLengthPrefixed4(avcc,nalus));CHECK(nalus.size()==3);
    avcc.pop_back();CHECK(!km::h264::SplitLengthPrefixed4(avcc,nalus));
    std::vector<uint8_t> emptyNal{0,0,1};CHECK(!km::h264::SplitAnnexB(emptyNal,nalus));
    std::vector<uint8_t> bad{0,0,1,0xff};CHECK(!km::h264::SplitAnnexB(bad,nalus));
    std::vector<uint8_t> huge(km::h264::kMaxAccessUnitBytes+1);CHECK(!km::h264::SplitAnnexB(huge,nalus));
    km::LatestFrame<int> slot;
    const auto old=slot.generation();CHECK(slot.publish(old,std::make_shared<const int>(7),10));
    CHECK(*slot.get(20,10)==7);CHECK(!slot.get(21,10));
    CHECK(!slot.publish(old,std::make_shared<const int>(8),9));
    const auto fresh=slot.reset();CHECK(fresh!=old);CHECK(!slot.get(20,100));
    CHECK(!slot.publish(old,std::make_shared<const int>(9),25));
    CHECK(slot.publish(fresh,std::make_shared<const int>(10),25));CHECK(*slot.get(25,0)==10);
    std::vector<uint8_t> sy{1,2,3,4,99,99,5,6,7,8,99,99};
    std::vector<uint8_t> suv{10,11,12,13,99,99};
    std::vector<uint8_t> dy(24,222),duv(12,222);
    km::Nv12View src{sy,suv,6,6,4,2};
    km::MutableNv12View dst{dy,duv,6,6,4,4};
    CHECK(km::Letterbox(src,dst,0));
    CHECK(dy[0]==1 && dy[3]==4 && dy[6]==5 && dy[9]==8 && dy[12]==16);
    CHECK(dy[4]==222 && duv[4]==222); // padding untouched
    CHECK(duv[0]==10 && duv[3]==13 && duv[6]==128);
    std::vector<uint8_t> ry(8),ruv(4);
    CHECK(km::Letterbox(src,{ry,ruv,2,2,2,4},90));
    CHECK((ry==std::vector<uint8_t>{5,1,6,2,7,3,8,4}));
    CHECK((ruv==std::vector<uint8_t>{10,11,12,13}));
    CHECK(km::Letterbox(src,{ry,ruv,2,2,2,4},270));
    CHECK((ry==std::vector<uint8_t>{4,8,3,7,2,6,1,5}));
    CHECK(km::Letterbox(src,{ry,ruv,4,4,4,2},180));
    CHECK((ry==std::vector<uint8_t>{8,7,6,5,4,3,2,1}));
    auto invalid=dst;invalid.strideY=3;CHECK(!km::FillBlack(invalid));
    invalid=dst;invalid.y=std::span<uint8_t>(dy.data(),5);CHECK(!km::FillBlack(invalid));
    CHECK(!km::Letterbox(src,dst,45));
    std::cout<<"common_contracts: all checks passed\n";
}
