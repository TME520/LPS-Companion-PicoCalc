// LPS-113-CHG06: JSON save writer and streaming parser, without the hardware adapter.
#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <string>
using namespace lps;
using lps_test::JsonStore;
static SaveCheck parse(const std::string& json,State& out,std::size_t chunk=7){
    lps_test::MemorySource source(json.data(),json.size(),chunk);SaveInfo info;return readSave(source,out,info);
}
static std::string signedJson(const std::string& json){JsonStore j;j.assign(json);return j.text();}
static std::string replaced(std::string json,const char* from,const std::string& to){
    const auto at=json.find(from);assert(at!=std::string::npos);json.replace(at,std::strlen(from),to);return json;
}
struct FailingSource final:SaveSource {
    std::string data;std::size_t position=0;
    long read(char* out,std::size_t capacity)override{
        if(position>=40)return -1; // I/O error part-way through the file.
        std::size_t n=data.size()-position;if(n>capacity)n=capacity;if(n>8)n=8;
        std::memcpy(out,data.data()+position,n);position+=n;return long(n);
    }
};
struct FailingSink final:SaveSink {
    std::size_t budget=300;
    bool write(const char*,std::size_t n)override{if(n>budget)return false;budget-=n;return true;}
};
int main(){
    // 1. Round trip of a fully populated state, whatever the read chunk size.
    const State full=lps_test::fullState();
    JsonStore store=lps_test::encodeJson(full);
    const std::string json=store.text();
    for(std::size_t chunk:{std::size_t(1),std::size_t(7),std::size_t(256),std::size_t(4096)}){
        State back;assert(parse(json,back,chunk)==SaveCheck::Valid);assert(lps_test::sameState(full,back));
        // History is stored oldest first and reloaded as a straight ring.
        assert(back.next==0&&back.count==HistoryLength&&back.history[0].number==60+7);
    }
    assert(json.rfind("{\"format\":\"lps-companion\",\"version\":2,\"generation\":1,\n\"state\":{",0)==0);
    assert(json.find("\"language\":\"fr\",\"palette\":\"pink\",\"xp\":123456")!=std::string::npos);
    assert(json.find("\"activities\":[\"Marche\",\"Régime\",\"Bible\"")!=std::string::npos);
    assert(json.find("\"note\":\"Note 100: \\\"quoted\\\" back\\\\slash x\"")!=std::string::npos);
    assert(json.find("\"date\":null")!=std::string::npos&&json.find("\"expected\":null")!=std::string::npos);
    assert(json.size()>20&&json.compare(json.size()-20,9,"\"crc32\":\"")==0&&json.compare(json.size()-2,2,"}\n")==0);
    std::printf("Full save size: %zu bytes\n",json.size());
    assert(json.size()<SaveMaxBytes);
    // Worst case: every text field full of characters that need escaping.
    {
        State worst=full;
        auto stuff=[](auto& text){text.fill(0);for(std::size_t i=0;i+1<text.size();++i)text[i]=i%2?'"':'\\';};
        stuff(worst.today.note);for(Day& d:worst.history){stuff(d.note);d.flags=ActivityMask;}
        for(Task& t:worst.tasks)stuff(t.name);for(Reminder& r:worst.reminders)stuff(r.text);
        worst.hiddenHome=uint16_t(HomeAll&~HomeLocked); // LPS-115-CHG05: every hideable home entry
        const JsonStore big=lps_test::encodeJson(worst);State back;
        assert(parse(big.text(),back)==SaveCheck::Valid&&lps_test::sameState(worst,back));
        std::printf("Worst-case save size: %zu bytes\n",big.size);assert(big.size<SaveMaxBytes/2);
    }
    // Generations are written as given and returned by the reader.
    {lps_test::MemorySink sink(store.bytes,store.size);store.size=0;uint32_t crc=0;assert(writeSave(full,4000000000u,sink,crc));
     lps_test::MemorySource source(store.bytes.data(),store.size);State back;SaveInfo info;
     assert(readSave(source,back,info)==SaveCheck::Valid&&info.generation==4000000000u&&info.checksum==crc);}
    // A failing sink is reported.
    {FailingSink sink;uint32_t crc=0;assert(!writeSave(full,1,sink,crc));}
    // 2. Every truncation is rejected, never accepted as a shorter save.
    for(std::size_t n=0;n+1<json.size();++n){State back;assert(parse(json.substr(0,n),back,64)==SaveCheck::Damaged);}
    // The final newline is optional whitespace.
    {State back;assert(parse(json.substr(0,json.size()-1),back)==SaveCheck::Valid);}
    // 3. Every single-bit flip is caught (CRC-32 or syntax).
    for(std::size_t i=0;i<json.size();++i){
        std::string bad=json;bad[i]=char(bad[i]^0x04);State back;const auto r=parse(bad,back,128);
        assert(r!=SaveCheck::Valid);
    }
    // 4. Checksum mismatch: a well-formed file whose crc32 is wrong.
    {std::string bad=json;const auto at=bad.rfind("\"crc32\":\"")+9;bad[at]=bad[at]=='0'?'1':'0';State back;assert(parse(bad,back)==SaveCheck::Damaged);}
    // A file without crc32, or with a member after it, is rejected.
    {State back;assert(parse(replaced(json,"\"crc32\"","\"crc33\""),back)==SaveCheck::Damaged);}
    {std::string extra=json;extra.insert(extra.rfind('}'),",\"late\":1");State back;assert(parse(extra,back)==SaveCheck::Damaged);}
    {State back;assert(parse(json+"x",back)==SaveCheck::Damaged);}
    // 5. Unknown keys are skipped at every level, including nested values.
    {
        std::string future=replaced(json,"\"state\":{","\"writer\":{\"name\":\"LPS\",\"list\":[1,2.5e3,-4,true,false,null,{\"a\":[[]]}]},\"state\":{\"mood\":\"ok\",");
        future=replaced(future,"\"note\":","\"weather\":[\"sun\",{\"t\":21}],\"note\":");
        future=replaced(future,"\"status\":","\"colour\":\"red\",\"status\":");
        future=replaced(future,"\"days\":","\"repeat\":{\"every\":\"week\"},\"days\":");
        future=replaced(future,"\"crc32\"","\"a very long unknown key that overflows the key buffer\":\""+std::string(300,'z')+"\",\"crc32\"");
        State back;assert(parse(signedJson(future),back)==SaveCheck::Valid);assert(lps_test::sameState(full,back));
        // Without the recomputed crc32 the same edit is refused.
        assert(parse(future,back)==SaveCheck::Damaged);
    }
    // Unknown activity names are ignored; known ones map back to their bits.
    {
        State s;s.today.flags=(1u<<0)|(1u<<9);JsonStore j=lps_test::encodeJson(s);std::string t=j.text();
        assert(t.find("\"activities\":[\"Marche\",\"Congé\"]")!=std::string::npos);
        t=replaced(t,"\"activities\":[\"Marche\",","\"activities\":[\"Piano\",\"Marche\",\"Walk\",");
        State back;assert(parse(signedJson(t),back)==SaveCheck::Valid&&back.today.flags==((1u<<0)|(1u<<9)));
    }
    // LPS-113-CHG08: the six new activities round-trip by French name, bits 16-21.
    {
        State s;s.today.flags=(ActivityFlags(1u)<<15)|(ActivityFlags(1u)<<18)|(ActivityFlags(1u)<<21);
        s.history[0].flags=ActivityFlags(1u)<<16;s.history[0].number=7;s.count=1;s.next=1;
        JsonStore j=lps_test::encodeJson(s);const std::string t=j.text();
        assert(t.find("\"activities\":[\"Shopping\",\"Ménage\",\"Méditation\"]")!=std::string::npos);
        assert(t.find("\"activities\":[\"Sport\"]")!=std::string::npos);
        State back;assert(parse(t,back)==SaveCheck::Valid&&lps_test::sameState(s,back));
        assert(back.today.flags==s.today.flags&&back.history[0].flags==(ActivityFlags(1u)<<16));
        // Every activity, in list order.
        s.today.flags=ActivityMask;j=lps_test::encodeJson(s);
        assert(j.text().find("\"Shopping\",\"Sport\",\"Cuisine\",\"Ménage\",\"Famille\",\"Amis\",\"Méditation\"]")!=std::string::npos);
        assert(parse(j.text(),back)==SaveCheck::Valid&&back.today.flags==ActivityMask);
        assert(DefaultActivityCount==22&&activityPages(DefaultActivityCount)==3);
    }
    // 6. String escaping: quotes, backslashes, control characters and UTF-8.
    {
        State s;std::strcpy(s.today.note.data(),"Café «déjà» \"q\" \\ /\t\x01 end");
        std::strcpy(s.tasks[0].name.data(),"Écrire \"ça\"");s.taskCount=1;
        JsonStore j=lps_test::encodeJson(s);const std::string t=j.text();
        assert(t.find("\"note\":\"Café «déjà» \\\"q\\\" \\\\ /\\t\\u0001 end\"")!=std::string::npos);
        assert(t.find("\"name\":\"Écrire \\\"ça\\\"\"")!=std::string::npos);
        // Control characters are escaped on write but refused in stored text on read.
        State back;assert(parse(t,back)==SaveCheck::Damaged);
        s.today.note.fill(0);std::strcpy(s.today.note.data(),"Café «déjà» \"q\" \\ / end");j=lps_test::encodeJson(s);
        assert(parse(j.text(),back)==SaveCheck::Valid&&lps_test::sameState(s,back));
        // \u escapes (including a surrogate pair) decode to UTF-8; "\/" is a slash.
        std::string u=replaced(j.text(),"\"note\":\"Café","\"note\":\"Caf\\u00e9 \\ud83d\\ude00 \\/");
        assert(parse(signedJson(u),back)==SaveCheck::Valid);
        assert(std::strncmp(back.today.note.data(),"Café \xf0\x9f\x98\x80 / «déjà»",std::strlen("Café \xf0\x9f\x98\x80 / «déjà»"))==0);
        // Lone surrogates, \u0000 and bad escapes are malformed.
        assert(parse(signedJson(replaced(j.text(),"\"note\":\"Café","\"note\":\"\\ud83d")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(j.text(),"\"note\":\"Café","\"note\":\"\\u0000")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(j.text(),"\"note\":\"Café","\"note\":\"\\x")),back)==SaveCheck::Damaged);
    }
    // 7. Field limits are enforced like the binary loader did.
    {
        State s;JsonStore j=lps_test::encodeJson(s);const std::string t=j.text();State back;
        assert(parse(signedJson(replaced(t,"\"note\":\"\"",("\"note\":\""+std::string(97,'n')+"\""))),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"note\":\"\"",("\"note\":\""+std::string(96,'n')+"\""))),back)==SaveCheck::Valid);
        assert(parse(signedJson(replaced(t,"\"number\":1","\"number\":0")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"number\":1","\"number\":1.5")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"date\":\"2026-09-17\"","\"date\":\"2027-02-29\"")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"expected\":null","\"expected\":0")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"palette\":\"green\"","\"palette\":\"teal\"")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"format\":\"lps-companion\"","\"format\":\"other\"")),back)==SaveCheck::Damaged);
        std::string days;for(unsigned i=0;i<32;++i)days+=std::string(i?",":"")+"{\"number\":1}";
        assert(parse(signedJson(replaced(t,"\"history\":[]",("\"history\":["+days+"]"))),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"kanban\":[]","\"kanban\":[{\"status\":\"todo\"}]")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"reminders\":[]","\"reminders\":[{\"from\":\"2028-02-29\",\"days\":366,\"text\":\"x\"}]")),back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(t,"\"reminders\":[]","\"reminders\":[{\"from\":\"2028-02-29\",\"days\":365,\"text\":\"x\"}]")),back)==SaveCheck::Valid&&back.reminderCount==1);
        // Missing optional members fall back to defaults.
        assert(parse(signedJson(replaced(t,"\"xp\":0,","")),back)==SaveCheck::Valid&&back.xp==0);
    }
    // 8. Schema versions: a newer, intact file is Unsupported; a newer damaged one is Damaged.
    {
        State back;const std::string v2=replaced(json,"\"version\":2","\"version\":3");
        assert(parse(signedJson(replaced(v2,"\"state\":{","\"state\":{\"shape\":[\"anything\"],")),back)==SaveCheck::Unsupported);
        assert(parse(v2,back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(json,"\"version\":2","\"version\":0")),back)==SaveCheck::Damaged);
    }
    // 9. An I/O error part-way is reported as such, not as damage.
    {FailingSource source;source.data=json;State back;SaveInfo info;assert(readSave(source,back,info)==SaveCheck::IOError);}
    // 10. LPSC116-FEAT01-CHG02: "focus":true after name, DOS interop, no "focus":false.
    {
        auto slurp=[](const char* path){
            std::FILE* f=std::fopen(path,"rb");assert(f);
            std::fseek(f,0,SEEK_END);const long n=std::ftell(f);assert(n>0);std::rewind(f);
            std::string s(std::size_t(n),'\0');
            assert(std::fread(s.data(),1,std::size_t(n),f)==std::size_t(n));std::fclose(f);return s;
        };
        auto writeGen=[](const State& s,uint32_t generation){
            std::array<char,lps_test::MemoryCapacity> bytes{};std::size_t n=0;
            lps_test::MemorySink sink(bytes,n);uint32_t crc=0;
            assert(writeSave(s,generation,sink,crc));
            return std::string(bytes.data(),n);
        };
        auto parseInfo=[](const std::string& t,State& out,SaveInfo& info){
            lps_test::MemorySource source(t.data(),t.size());
            return readSave(source,out,info);
        };
        // A v1.15 save with no focus rewrites to the same bytes (generation kept).
        const std::string v115=slurp("tests/fixtures/picocalc_v115_custom.jsn");
        assert(v115.find("focus")==std::string::npos);
        State s115;SaveInfo i115;
        assert(parseInfo(v115,s115,i115)==SaveCheck::Valid);
        assert(s115.taskCount==3&&!s115.tasks[0].focus&&!s115.tasks[1].focus&&!s115.tasks[2].focus);
        const std::string v115out=writeGen(s115,i115.generation);
        assert(v115out==v115);
        assert(json.find("\"version\":2")!=std::string::npos); // schema unchanged
        // A real DOS v1.16 save with one focused card loads and rewrites identically.
        const std::string dos=slurp("tests/fixtures/dos_v116_focus.jsn");
        assert(dos.find("{\"status\":\"doing\",\"name\":\"Réparer le vélo\",\"focus\":true}")!=std::string::npos);
        assert(dos.find("\"focus\":false")==std::string::npos);
        State sdos;SaveInfo idos;
        assert(parseInfo(dos,sdos,idos)==SaveCheck::Valid);
        assert(sdos.taskCount==3&&sdos.tasks[0].focus&&sdos.tasks[0].status==TaskStatus::Doing);
        assert(!sdos.tasks[1].focus&&!sdos.tasks[2].focus&&sdos.tasks[2].status==TaskStatus::Done);
        const std::string dosout=writeGen(sdos,idos.generation);
        assert(dosout==dos);
        // "focus":false is accepted and omitted on write-back; a focus on a DONE
        // card is dropped; a malformed value damages the save, as a bad status does.
        State loaded;loaded.taskCount=3;
        loaded.tasks[0].status=TaskStatus::Todo;std::strcpy(loaded.tasks[0].name.data(),"Keep");
        loaded.tasks[1].status=TaskStatus::Doing;std::strcpy(loaded.tasks[1].name.data(),"Work");
        loaded.tasks[2].status=TaskStatus::Done;std::strcpy(loaded.tasks[2].name.data(),"Done");
        const std::string base=writeGen(loaded,1);
        auto inject=[&](const std::string& src,const char* after,const char* extra){
            std::string t=src;const auto at=t.find(after);assert(at!=std::string::npos);
            t.insert(at+std::strlen(after),extra);return signedJson(t);
        };
        const std::string mixed=inject(inject(inject(base,"\"name\":\"Keep\"",",\"focus\":false"),
            "\"name\":\"Work\"",",\"focus\":true"),"\"name\":\"Done\"",",\"focus\":true");
        State dec;assert(parse(mixed,dec)==SaveCheck::Valid);
        assert(dec.taskCount==3&&!dec.tasks[0].focus&&!std::strcmp(dec.tasks[0].name.data(),"Keep"));
        assert(dec.tasks[1].focus&&!std::strcmp(dec.tasks[1].name.data(),"Work"));
        assert(dec.tasks[2].status==TaskStatus::Done&&!dec.tasks[2].focus&&!std::strcmp(dec.tasks[2].name.data(),"Done"));
        const std::string back=writeGen(dec,1);
        assert(back.find("{\"status\":\"doing\",\"name\":\"Work\",\"focus\":true}")!=std::string::npos);
        assert(back.find("{\"status\":\"todo\",\"name\":\"Keep\"}")!=std::string::npos);
        assert(back.find("\"name\":\"Keep\",\"focus\"")==std::string::npos);
        assert(back.find("{\"status\":\"done\",\"name\":\"Done\"}")!=std::string::npos);
        assert(back.find("\"name\":\"Done\",\"focus\"")==std::string::npos);
        assert(back.find("\"focus\":false")==std::string::npos);
        for(const char* bad:{"1","\"true\"","null"}){
            const std::string damaged=inject(base,"\"name\":\"Keep\"",(std::string(",\"focus\":")+bad).c_str());
            State tmp;if(parse(damaged,tmp)!=SaveCheck::Damaged){std::fprintf(stderr,"focus value %s accepted\n",bad);assert(0);}
        }
        // Binary wire layout is unchanged; focus is not stored and decodes as 0.
        State wired;wired.taskCount=2;
        wired.tasks[0].status=TaskStatus::Doing;std::strcpy(wired.tasks[0].name.data(),"Fix the bike");wired.tasks[0].focus=true;
        wired.tasks[1].status=TaskStatus::Todo;std::strcpy(wired.tasks[1].name.data(),"Write docs");
        Blob wire=lps_test::encodeLegacyV6(wired);
        assert(wire.size==WireSize);
        State decoded;assert(decode(wire.bytes.data(),wire.size,decoded)&&decoded.taskCount==wired.taskCount);
        assert(!std::strcmp(decoded.tasks[0].name.data(),wired.tasks[0].name.data())&&decoded.tasks[0].status==wired.tasks[0].status&&!decoded.tasks[0].focus);
        assert(!std::strcmp(decoded.tasks[1].name.data(),"Write docs")&&!decoded.tasks[1].focus);
        // A fully populated state with focus on non-DONE cards round-trips; DONE stays unfocused.
        State focused=lps_test::fullState();
        for(unsigned i=0;i<focused.taskCount;++i)focused.tasks[i].focus=(focused.tasks[i].status!=TaskStatus::Done)&&(i%2==0);
        JsonStore stored=lps_test::encodeJson(focused);State again;
        assert(parse(stored.text(),again)==SaveCheck::Valid&&lps_test::sameState(focused,again));
        const std::string focusedJson=stored.text();
        assert(focusedJson.find("\"name\":\"Task 00 {json} [x] x\",\"focus\":true")!=std::string::npos);
        assert(focusedJson.find("\"name\":\"Task 02 {json} [x] x\"}")!=std::string::npos);
        assert(focusedJson.find("\"name\":\"Task 02 {json} [x] x\",\"focus\"")==std::string::npos);
        assert(json.find("\"focus\"")==std::string::npos); // unfocused fullState matches the v1.15 shape
    }
    std::puts("PASS: JSON round trip, oldest-first history, truncation and bit-flip rejection, crc32 mismatch, unknown keys, activity names, escaping/UTF-8, field limits, schema versions, I/O errors and Kanban focus.");
}
