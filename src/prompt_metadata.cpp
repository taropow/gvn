#include "gvn/prompt_metadata.h"
#include <windows.h>
#include <miniz.h>
#define PICOJSON_USE_INT64
#include "../third_party/picojson/picojson.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <tuple>
namespace gvn {
namespace {
constexpr size_t text_limit=1024*1024,total_limit=4*text_limit;
void require(bool value){if(!value)throw std::runtime_error("Invalid prompt metadata");}
std::wstring wide(const std::string &text) {
    if(text.empty())return {};
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    require(count>0);std::wstring out(count,L'\0');
    require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),count)>0);return out;
}
std::string latin_utf8(std::string_view text){std::string out;out.reserve(text.size());for(const unsigned char c:text){if(c<128)out+=char(c);else{out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}}return out;}
std::string inflate_text(std::span<const uint8_t> input,size_t &budget) {
    require(budget>0);std::string output(std::min(text_limit,budget)+1,'\0');mz_ulong size=static_cast<mz_ulong>(output.size());
    const auto result=mz_uncompress(reinterpret_cast<unsigned char*>(output.data()),&size,input.data(),static_cast<mz_ulong>(input.size()));
    const auto used=std::min(size_t(size),output.size());const bool fits=used<=budget;budget-=std::min(budget,used);
    require(result==MZ_OK&&fits&&size<=text_limit);output.resize(size);return output;
}
std::wstring trim(std::wstring text){const auto first=text.find_first_not_of(L" \r\n\t");if(first==text.npos)return {};return text.substr(first,text.find_last_not_of(L" \r\n\t")-first+1);}
std::pair<std::wstring,std::wstring> parameters(const std::wstring &text) {
    constexpr auto negative=L"Negative prompt:";const auto at=text.find(negative);
    if(at==text.npos)return {trim(text.substr(0,text.find(L"\nSteps:"))),{}};
    const auto start=at+wcslen(negative);auto tail=text.substr(start,text.find(negative,start)-start);
    auto end=tail.find(L"\nSteps:");if(end==tail.npos)end=tail.find(L'\n');
    return {trim(text.substr(0,at)),trim(tail.substr(0,end))};
}
using Value=picojson::value;using Object=picojson::object;using Array=picojson::array;
const Value *member(const Object &o,const char *key){const auto it=o.find(key);return it==o.end()?nullptr:&it->second;}
const Object &object(const Object &o,const char *key){static const Object empty;const auto v=member(o,key);if(!v)return empty;require(v->is<Object>());return v->get<Object>();}
const Array &array(const Object &o,const char *key){static const Array empty;const auto v=member(o,key);if(!v)return empty;require(v->is<Array>()&&v->get<Array>().size()<=256);return v->get<Array>();}
std::wstring string(const Object &o,const char *key){const auto v=member(o,key);if(!v)return {};require(v->is<std::string>());return wide(v->get<std::string>());}
int64_t integer(const Object &o,const char *key){const auto v=member(o,key);if(!v)return 0;require(v->is<int64_t>()&&v->get<int64_t>()>=0&&v->get<int64_t>()<INT64_MAX);return v->get<int64_t>();}
double number(const Object &o,const char *key){const auto v=member(o,key);if(!v)return 0;require(v->is<double>()&&std::isfinite(v->get<double>()));return v->get<double>();}
bool boolean(const Object &o,const char *key){const auto v=member(o,key);if(!v)return false;require(v->is<bool>());return v->get<bool>();}
void add(PromptDocument &d,std::wstring group,std::wstring name,std::wstring value,bool muted=false){d.fields.push_back({std::move(group),std::move(name),std::move(value),muted});}
void structured(PromptDocument &d,const Object &o) {
    const auto profile=string(o,"profile");d.status=L"Rust Inpainter"+(profile.empty()?L"":L" · "+profile)+L" — 対応する内訳あり";
    const auto &slot=object(o,"slot"),&main=object(o,"main"),&tab=object(o,"tab"),&sent=object(o,"final"),&lora=object(o,"lora");
    for(const auto &[group,source]:std::initializer_list<std::pair<std::wstring,const Object*>>{{L"スロット",&slot},{L"メイン",&main}}){
        add(d,group,L"プロンプト",string(*source,"prompt"));add(d,group,L"ネガティブ",string(*source,"negative"));
        if(group==L"スロット"){add(d,group,L"日本語原文",string(*source,"jp"));add(d,group,L"日本語の翻訳",string(*source,"jp_translated"));}
    }
    const auto name=string(tab,"name");const auto character=L"キャラ タブ"+std::to_wstring(integer(tab,"index")+1)+(name.empty()?L"":L" · "+name);
    add(d,character,L"プロンプト",string(tab,"prompt"));add(d,character,L"日本語原文",string(tab,"jp"));add(d,character,L"日本語の翻訳",string(tab,"jp_translated"));
    const auto id=string(tab,"id");if(!id.empty())add(d,L"詳細",L"キャラタブID",id);
    std::wstring enabled;
    for(const auto &entry:array(tab,"toggles")){require(entry.is<Object>());const auto &t=entry.get<Object>();const bool on=boolean(t,"enabled");const auto prompt=string(t,"prompt"),title=string(t,"title");
        add(d,character,(on?L"ON · ":L"OFF · ")+(title.empty()?L"トグル":title),prompt,!on);
        if(on&&!prompt.empty()){if(!enabled.empty())enabled+=L", ";enabled+=prompt;}}
    add(d,character,L"ONトグルのみ",enabled);
    size_t n{};
    for(const auto &entry:array(o,"extended")){++n;require(entry.is<Object>());const auto &e=entry.get<Object>();const auto tab_name=string(e,"tab_name"),title=string(e,"title");
        const auto group=tab_name.empty()?L"拡張"+std::to_wstring(integer(e,"tab")+1):tab_name;
        (void)integer(e,"tab");const auto weight=number(e,"weight");std::wostringstream label;label<<L"["<<group<<L"] "<<(title.empty()?L"項目"+std::to_wstring(n):title)<<L" · 重み "<<std::setprecision(12)<<weight;
        add(d,L"拡張プロンプト",label.str(),string(e,"prompt"));}
    if(array(o,"extended").empty())add(d,L"拡張プロンプト",L"有効だった項目",{});
    for(const auto &[key,label]:std::initializer_list<std::pair<const char*,const wchar_t*>>{{"speed",L"速度用"},{"boost",L"ブースト用"},{"extra",L"追加"}}){const auto value=string(lora,key);if(!value.empty())add(d,L"自動で付いたLoRA",label,value);}
    const auto adetailer=string(o,"adetailer"),memo=string(o,"memo");if(!adetailer.empty())add(d,L"補足",L"ADetailer（設定文字列）",adetailer);if(!memo.empty())add(d,L"補足",L"メモ",memo);
    d.positive=string(sent,"prompt");d.negative=string(sent,"negative");add(d,L"送信済み全文",L"ポジティブ",d.positive);add(d,L"送信済み全文",L"ネガティブ",d.negative);
}
}
std::shared_ptr<const PromptSource> read_png_prompt_source(std::span<const uint8_t> bytes) {
    if(bytes.size()<8||std::memcmp(bytes.data(),"\x89PNG\r\n\x1a\n",8))return {};
    auto out=std::make_shared<PromptSource>();size_t budget=total_limit,text_chunks{};bool relevant{};
    const auto be=[&](size_t p){return (uint32_t(bytes[p])<<24)|(uint32_t(bytes[p+1])<<16)|(uint32_t(bytes[p+2])<<8)|bytes[p+3];};
    try {
        size_t p=8;
        for(size_t chunks=0;p<bytes.size();++chunks){require(chunks<100000&&bytes.size()-p>=12);const size_t length=be(p);require(length<=bytes.size()-p-12);
            const auto type=bytes.subspan(p+4,4),data=bytes.subspan(p+8,length);const bool text=!std::memcmp(type.data(),"iTXt",4)||!std::memcmp(type.data(),"tEXt",4)||!std::memcmp(type.data(),"zTXt",4);
            if(text){require(++text_chunks<=256&&budget>0);try{
                require(length<=text_limit+512);auto crc=mz_crc32(MZ_CRC32_INIT,type.data(),4);crc=mz_crc32(crc,data.data(),data.size());require(uint32_t(crc)==be(p+8+length));
                size_t z{};while(z<data.size()&&data[z])++z;require(z>0&&z<=79&&z<data.size());
                const std::string key(reinterpret_cast<const char*>(data.data()),z);std::string lower=key;
                std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
                relevant=relevant||key=="inpainter-parts"||key=="parameters"||key=="Generation time"||key=="generation_backend"||lower.find("prompt")!=lower.npos||lower.find("negative")!=lower.npos;
                std::string value;bool utf8=key=="inpainter-parts",inflated{};
                if(!std::memcmp(type.data(),"iTXt",4)){utf8=true;size_t at=z+1;require(data.size()-at>=2&&data[at]<=1&&data[at+1]==0);const bool compressed=data[at]!=0;at+=2;
                    for(int field=0;field<2;++field){while(at<data.size()&&data[at])++at;require(at<data.size());++at;}
                    if(compressed){value=inflate_text(data.subspan(at),budget);inflated=true;}else value.assign(reinterpret_cast<const char*>(data.data()+at),data.size()-at);
                }else if(!std::memcmp(type.data(),"zTXt",4)){require(z+2<=data.size()&&data[z+1]==0);value=inflate_text(data.subspan(z+2),budget);inflated=true;}
                else value.assign(reinterpret_cast<const char*>(data.data()+z+1),data.size()-z-1);
                if(!inflated){require(value.size()<=budget);budget-=value.size();}
                require(value.size()<=text_limit&&value.find('\0')==value.npos);if(!utf8)value=latin_utf8(value);else (void)wide(value);
                require(out->byte_size+key.size()+value.size()<=total_limit&&out->text.size()<256);
                out->byte_size+=key.size()+value.size();out->text.emplace_back(latin_utf8(key),std::move(value));
            }catch(const std::exception &){out->issue="一部のPNGテキストが破損しているか、読み取り上限を超えています。";}}
            if(!std::memcmp(type.data(),"IEND",4))break;p+=length+12;
        }
    }catch(const std::exception &){out->issue="PNGメタデータの構造が不正、または走査上限を超えています。";}
    if((!relevant||out->text.empty())&&out->issue.empty())return {};out->byte_size+=out->issue.size();return out;
}
PromptDocument describe_prompts(const std::shared_ptr<const PromptSource> &source) {
    PromptDocument out;out.status=L"内訳なし";if(!source){out.status=L"プロンプト情報なし";add(out,L"全文",L"プロンプト",{});return out;}
    std::map<std::string,std::wstring> normal;std::string raw;bool has_parts{};
    for(const auto &[key,value]:source->text){if(key=="inpainter-parts"){if(!has_parts){has_parts=true;raw=value;}}else normal[key]=wide(value);}
    if(has_parts)try{
        require(raw.size()<=text_limit);size_t nodes{};bool quoted{},escape{};for(const auto c:raw){if(escape){escape=false;continue;}if(quoted&&c=='\\'){escape=true;continue;}if(c=='"')quoted=!quoted;if(!quoted&&(c==','||c=='{'||c=='['))require(++nodes<=8192);}
        Value json;std::string error;const auto end=picojson::parse(json,raw.begin(),raw.end(),&error);
        require(error.empty()&&std::all_of(end,raw.end(),[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';})&&json.is<Object>());const auto &o=json.get<Object>();const auto version=integer(o,"version");
        if(version!=1){out.state=PromptPartsState::Unsupported;out.status=L"未対応バージョン: "+std::to_wstring(version);}
        else{PromptDocument parsed;structured(parsed,o);parsed.state=PromptPartsState::Supported;out=std::move(parsed);}
    }catch(const std::exception &){out.state=PromptPartsState::Invalid;out.status=L"内訳の解析失敗";}
    if(out.state!=PromptPartsState::Supported){
        if(normal.contains("parameters"))std::tie(out.positive,out.negative)=parameters(normal["parameters"]);
        else if(normal.contains("prompt")){out.positive=normal["prompt"];out.negative=normal["negative_prompt"];}
        else for(const auto &[key,value]:normal){auto lower=key;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});if(lower.find("negative")!=lower.npos)out.negative=value;else if(lower.find("prompt")!=lower.npos)out.positive=value;}
        add(out,L"全文",L"ポジティブ",out.positive);add(out,L"全文",L"ネガティブ",out.negative);
    }
    if(normal.contains("parameters"))add(out,L"生成側の記録",L"parameters（元の生成設定を含む）",normal["parameters"]);
    for(const auto &[key,value]:normal)add(out,(key=="Generation time"||key=="generation_backend")?L"補足":L"元メタデータ",wide(key),value);
    if(has_parts)add(out,L"詳細",L"元JSON · inpainter-parts",wide(raw));
    if(!source->issue.empty())add(out,L"詳細",L"読み取り状態",wide(source->issue));
    if(out.state==PromptPartsState::Absent&&out.positive.empty()&&out.negative.empty())out.status=L"プロンプト情報なし · 内訳なし";
    return out;
}
}
