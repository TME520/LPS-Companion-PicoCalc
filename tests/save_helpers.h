#pragma once
// Test-only helpers. The application no longer writes the binary format
// (LPS-113-CHG06); this copy of the v1.12 format-6 encoder lets the tests
// build legacy SAVE_A/B.BIN payloads to check the migration to JSON.
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
namespace lps_test {
inline lps::Blob encodeLegacyV6(const lps::State& s) {
    using namespace lps;
    Blob b;
    auto put=[&](uint32_t v,unsigned n){while(n--){b.bytes[b.size++]=uint8_t(v);v>>=8;}};
    put(0x3153504c,4);put(6,2);put(s.xp,4);put(s.next,1);put(s.count,1);put(uint8_t(s.language),1);put(uint8_t(s.palette),1);
    auto day=[&](const Day& d){put(d.date.year,2);put(d.date.month,1);put(d.date.day,1);put(d.number,4);put(d.flags,2);put(d.expected<0?0xffffffffu:uint32_t(d.expected),4);put(d.actual<0?0xffffffffu:uint32_t(d.actual),4);for(char c:d.note)put(uint8_t(c),1);};
    day(s.today);for(const Day& d:s.history)day(d);
    put(s.taskCount,1);for(const Task& task:s.tasks){put(uint8_t(task.status),1);for(char c:task.name)put(uint8_t(c),1);}
    put(s.reminderCount,1);for(const Reminder& r:s.reminders){put(r.from.year,2);put(r.from.month,1);put(r.from.day,1);put(r.duration,2);for(char c:r.text)put(uint8_t(c),1);}
    const auto crc=crc32(b.bytes.data(),b.size);put(crc,4);return b;
}
// In-memory JSON save buffer.
constexpr std::size_t MemoryCapacity=65536;
struct MemorySink final:lps::SaveSink {
    std::array<char,MemoryCapacity>& bytes;std::size_t& size;
    MemorySink(std::array<char,MemoryCapacity>& b,std::size_t& n):bytes(b),size(n){}
    bool write(const char* data,std::size_t n)override{if(size+n>bytes.size())return false;std::memcpy(bytes.data()+size,data,n);size+=n;return true;}
};
struct MemorySource final:lps::SaveSource {
    const char* data;std::size_t size,position=0,chunk;
    MemorySource(const char* d,std::size_t n,std::size_t c=7):data(d),size(n),chunk(c){}
    // Deliberately short reads so tokens straddle buffer refills.
    long read(char* out,std::size_t capacity)override{
        std::size_t n=size-position;if(n>capacity)n=capacity;if(n>chunk)n=chunk;
        std::memcpy(out,data+position,n);position+=n;return long(n);
    }
};
// A whole JSON save held in memory, for the test platforms.
struct JsonStore {
    std::array<char,MemoryCapacity> bytes{};std::size_t size=0;uint32_t generation=0;
    bool write(const lps::State& state){
        static std::array<char,MemoryCapacity> scratch{};std::size_t n=0;
        MemorySink sink(scratch,n);uint32_t checksum=0;
        if(!lps::writeSave(state,generation+1,sink,checksum))return false;
        bytes=scratch;size=n;++generation;return true;
    }
    lps::SaveCheck check(lps::State& state){
        MemorySource source(bytes.data(),size);lps::SaveInfo info;
        const auto result=lps::readSave(source,state,info);
        if(result==lps::SaveCheck::Valid)generation=info.generation;
        return result;
    }
    lps::LoadResult read(lps::State& state){return check(state)==lps::SaveCheck::Valid?lps::LoadResult::Ok:lps::LoadResult::Error;}
    std::string text()const{return std::string(bytes.data(),size);}
    // Replace the whole content, then recompute "crc32" over every byte before it.
    void assign(const std::string& json,bool resign=true){
        assert(json.size()<bytes.size());bytes.fill(0);std::memcpy(bytes.data(),json.data(),json.size());size=json.size();
        if(!resign)return;
        const auto at=json.rfind("\"crc32\":\"");assert(at!=std::string::npos&&at+17<=json.size());
        const uint32_t crc=lps::crc32(reinterpret_cast<const uint8_t*>(bytes.data()),at);
        char hex[12];std::snprintf(hex,sizeof hex,"%08lx",static_cast<unsigned long>(crc));std::memcpy(bytes.data()+at+9,hex,8);
    }
};
inline JsonStore encodeJson(const lps::State& state){JsonStore j;bool ok=j.write(state);assert(ok);(void)ok;return j;}
// Logical equality: history compared oldest to newest, lists up to their counts.
inline bool sameDay(const lps::Day& a,const lps::Day& b){
    return a.date.year==b.date.year&&a.date.month==b.date.month&&a.date.day==b.date.day&&a.number==b.number
        &&a.flags==b.flags&&a.expected==b.expected&&a.actual==b.actual&&a.note==b.note;
}
inline const lps::Day& archived(const lps::State& s,unsigned i){return s.history[(s.next+lps::HistoryLength-s.count+i)%lps::HistoryLength];}
inline bool sameState(const lps::State& a,const lps::State& b){
    if(a.language!=b.language||a.palette!=b.palette||a.xp!=b.xp||!sameDay(a.today,b.today)||a.count!=b.count
       ||a.taskCount!=b.taskCount||a.reminderCount!=b.reminderCount)return false;
    for(unsigned i=0;i<a.count;++i)if(!sameDay(archived(a,i),archived(b,i)))return false;
    for(unsigned i=0;i<a.taskCount;++i)if(a.tasks[i].status!=b.tasks[i].status||a.tasks[i].name!=b.tasks[i].name)return false;
    for(unsigned i=0;i<a.reminderCount;++i){
        const auto& x=a.reminders[i];const auto& y=b.reminders[i];
        if(x.from.year!=y.from.year||x.from.month!=y.from.month||x.from.day!=y.from.day||x.duration!=y.duration||x.text!=y.text)return false;
    }
    return true;
}
// Every field populated; the history ring has wrapped (next is not 0).
inline lps::State fullState(char fill='x'){
    using namespace lps;
    State s;s.language=Language::French;s.palette=Palette::Pink;s.xp=123456;
    auto day=[&](Day& d,unsigned n){
        d.date={2026,uint8_t(1+n%12),uint8_t(1+n%28)};d.number=n;d.flags=uint16_t((n*2654435761u)>>16);
        d.expected=n%3?int32_t(100000+n):-1;d.actual=n%4?int32_t(90000+n):-1;
        std::snprintf(d.note.data(),d.note.size(),"Note %u: \"quoted\" back\\slash %c",n,fill);
    };
    day(s.today,100);s.today.flags=0xffff;
    for(unsigned i=0;i<HistoryLength;++i)day(s.history[i],60+i);
    s.history[3].date={};s.history[4].note.fill(0);
    s.next=7;s.count=HistoryLength;
    s.taskCount=TaskCapacity;
    for(unsigned i=0;i<TaskCapacity;++i){s.tasks[i].status=TaskStatus(i%3);std::snprintf(s.tasks[i].name.data(),s.tasks[i].name.size(),"Task %02u {json} [x] %c",i,fill);}
    s.reminderCount=ReminderCapacity;
    for(unsigned i=0;i<ReminderCapacity;++i){s.reminders[i].from={2028,2,uint8_t(27+i)};s.reminders[i].duration=uint16_t(1+i*100);std::snprintf(s.reminders[i].text.data(),s.reminders[i].text.size(),"Reminder %u: a,b;c",i);}
    return s;
}
}
