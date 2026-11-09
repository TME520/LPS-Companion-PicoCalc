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
    assert(json.rfind("{\"format\":\"lps-companion\",\"version\":1,\"generation\":1,\n\"state\":{",0)==0);
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
        assert(ActivityCount==22&&PageCount==3);
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
        State back;const std::string v2=replaced(json,"\"version\":1","\"version\":2");
        assert(parse(signedJson(replaced(v2,"\"state\":{","\"state\":{\"shape\":[\"anything\"],")),back)==SaveCheck::Unsupported);
        assert(parse(v2,back)==SaveCheck::Damaged);
        assert(parse(signedJson(replaced(json,"\"version\":1","\"version\":0")),back)==SaveCheck::Damaged);
    }
    // 9. An I/O error part-way is reported as such, not as damage.
    {FailingSource source;source.data=json;State back;SaveInfo info;assert(readSave(source,back,info)==SaveCheck::IOError);}
    std::puts("PASS: JSON round trip, oldest-first history, truncation and bit-flip rejection, crc32 mismatch, unknown keys, activity names, escaping/UTF-8, field limits, schema versions and I/O errors.");
}
