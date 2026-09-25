#include <pandoeditor/giszip.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <zlib.h>

namespace pandoeditor {
namespace {
constexpr std::uint64_t maxInput=512ull*1024*1024;
constexpr std::uint64_t maxOutput=1024ull*1024*1024;
constexpr std::size_t maxEntries=10000;
void require(bool ok) {
    if(!ok)throw std::invalid_argument("INVALID_GIS_ZIP");
}
bool fits(std::size_t pos,std::size_t size,std::size_t end) {
    return pos<=end&&size<=end-pos;
}
std::uint16_t u16(std::string_view s,std::size_t at) {
    require(fits(at,2,s.size()));
    return static_cast<unsigned char>(s[at])|
           (static_cast<unsigned char>(s[at+1])<<8);
}
std::uint32_t u32(std::string_view s,std::size_t at) {
    return u16(s,at)|(std::uint32_t(u16(s,at+2))<<16);
}
std::string safePath(std::string_view path) {
    require(!path.empty()&&path.front()!='/'&&path.front()!='\\'&&
            path.find('\0')==path.npos);
    std::string normalized(path);
    std::replace(normalized.begin(),normalized.end(),'\\','/');
    require(!(normalized.size()>1&&normalized[1]==':'&&
              std::isalpha(static_cast<unsigned char>(normalized.front()))));
    std::size_t start=0;
    while(start<normalized.size()) {
        const auto end=normalized.find('/',start);
        const auto part=normalized.substr(start,end==std::string::npos?end:end-start);
        require(part!="..");
        if(end==std::string::npos)break;
        start=end+1;
    }
    return normalized;
}
std::string inflateDeflate(std::string_view compressed,std::uint32_t expected) {
    require(compressed.size()<=std::numeric_limits<uInt>::max()&&
            expected<=std::numeric_limits<uInt>::max());
    std::string output(expected ? expected : 1,'\0');
    z_stream stream{};
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in=static_cast<uInt>(compressed.size());
    stream.next_out=reinterpret_cast<Bytef*>(output.data());
    stream.avail_out=static_cast<uInt>(output.size());
    require(inflateInit2(&stream,-MAX_WBITS)==Z_OK);
    const auto status=inflate(&stream,Z_FINISH);
    const bool valid=status==Z_STREAM_END&&stream.total_in==compressed.size()&&
                     stream.total_out==expected;
    inflateEnd(&stream);
    require(valid);
    output.resize(expected);
    return output;
}
void put16(std::string& out,std::uint16_t value) {
    out.push_back(char(value));out.push_back(char(value>>8));
}
void put32(std::string& out,std::uint32_t value) {
    put16(out,std::uint16_t(value));put16(out,std::uint16_t(value>>16));
}
std::string deflateRaw(std::string_view input) {
    require(input.size()<=std::numeric_limits<uInt>::max());
    z_stream stream{};
    require(deflateInit2(&stream,6,Z_DEFLATED,-MAX_WBITS,8,Z_DEFAULT_STRATEGY)==Z_OK);
    const auto capacity=deflateBound(&stream,static_cast<uLong>(input.size()));
    if(capacity>std::numeric_limits<uInt>::max()) {deflateEnd(&stream);require(false);}
    std::string output(capacity,'\0');
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in=static_cast<uInt>(input.size());
    stream.next_out=reinterpret_cast<Bytef*>(output.data());
    stream.avail_out=static_cast<uInt>(output.size());
    const auto result=deflate(&stream,Z_FINISH);
    const auto size=stream.total_out;
    deflateEnd(&stream);
    require(result==Z_STREAM_END);
    output.resize(size);return output;
}
}

GisZipArchive readGisZipArchive(std::string_view bytes) {
    require(bytes.size()>=22&&bytes.size()<=maxInput);
    // The EOCD may follow up to 65535 comment bytes.
    const std::size_t first=bytes.size()>22+65535?bytes.size()-(22+65535):0;
    std::size_t eocd=bytes.npos;
    for(std::size_t i=bytes.size()-22;;--i) {
        if(u32(bytes,i)==0x06054b50&&i+22+u16(bytes,i+20)==bytes.size()) {
            eocd=i;break;
        }
        if(i==first)break;
    }
    require(eocd!=bytes.npos);
    require(u16(bytes,eocd+4)==0&&u16(bytes,eocd+6)==0);
    const auto count=u16(bytes,eocd+10);
    require(count<=maxEntries&&u16(bytes,eocd+8)==count);
    const auto centralSize=u32(bytes,eocd+12),centralStart=u32(bytes,eocd+16);
    require(centralSize!=0xffffffff&&centralStart!=0xffffffff&&
            fits(centralStart,centralSize,eocd)&&centralStart+centralSize==eocd);
    GisZipArchive result;
    result.entries.reserve(count);
    std::set<std::string> paths;
    std::uint64_t total=0;
    std::size_t cursor=centralStart;
    for(std::size_t i=0;i<count;++i) {
        require(fits(cursor,46,eocd)&&u32(bytes,cursor)==0x02014b50);
        const auto flags=u16(bytes,cursor+8),method=u16(bytes,cursor+10);
        const auto checksum=u32(bytes,cursor+16);
        const auto compressed=u32(bytes,cursor+20),expanded=u32(bytes,cursor+24);
        const auto nameSize=u16(bytes,cursor+28),extra=u16(bytes,cursor+30);
        const auto comment=u16(bytes,cursor+32);
        const auto local=u32(bytes,cursor+42);
        const auto entryEnd=std::uint64_t(cursor)+46+nameSize+extra+comment;
        require(entryEnd<=std::uint64_t(centralStart)+centralSize&&
                nameSize>0&&compressed!=0xffffffff&&expanded!=0xffffffff&&
                local!=0xffffffff&&u16(bytes,cursor+34)==0&&
                (flags&~std::uint16_t(0x0808))==0&&(method==0||method==8));
        const auto path=safePath(bytes.substr(cursor+46,nameSize));
        std::string key=path;
        std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
        require(paths.insert(key).second);
        total+=expanded;
        require(total<=maxOutput&&
                (bytes.empty()||total<=std::uint64_t(bytes.size())*100));
        require(fits(local,30,centralStart)&&u32(bytes,local)==0x04034b50);
        const auto localName=u16(bytes,local+26),localExtra=u16(bytes,local+28);
        require(u16(bytes,local+6)==flags&&u16(bytes,local+8)==method&&
                localName==nameSize);
        const auto data=std::uint64_t(local)+30+localName+localExtra;
        require(data<=centralStart&&compressed<=centralStart-data&&
                bytes.substr(local+30,nameSize)==bytes.substr(cursor+46,nameSize));
        const auto source=bytes.substr(std::size_t(data),compressed);
        require(method!=0||compressed==expanded);
        auto output=method==8?inflateDeflate(source,expanded):std::string(source);
        const auto computed=crc32(0,reinterpret_cast<const Bytef*>(output.data()),
                                  static_cast<uInt>(output.size()));
        require(computed==checksum);
        if(!path.empty()&&path.back()!='/')result.entries.push_back({path,std::move(output)});
        cursor=std::size_t(entryEnd);
    }
    require(cursor==std::uint64_t(centralStart)+centralSize);
    return result;
}
std::string writeGisZipArchive(const GisZipArchive& archive) {
    require(!archive.entries.empty()&&archive.entries.size()<=maxEntries);
    std::string output,central;
    std::set<std::string> paths;
    std::uint64_t expanded=0;
    for(const auto& entry:archive.entries) {
        const auto path=safePath(entry.path);
        require(path.size()<=std::numeric_limits<std::uint16_t>::max()&&path.back()!='/'&&
                entry.bytes.size()<=std::numeric_limits<std::uint32_t>::max());
        std::string key=path;
        std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
        require(paths.insert(key).second);
        expanded+=entry.bytes.size();require(expanded<=maxOutput);
        auto compressed=deflateRaw(entry.bytes);
        const bool useDeflate=compressed.size()<entry.bytes.size()&&
            (entry.bytes.size()<=6000||compressed.size()*100>=entry.bytes.size());
        const auto& payload=useDeflate?compressed:entry.bytes;
        const std::uint16_t method=useDeflate?8:0;
        require(payload.size()<=std::numeric_limits<std::uint32_t>::max()&&
                output.size()+30+path.size()+payload.size()<=maxInput);
        const auto offset=static_cast<std::uint32_t>(output.size());
        const auto crc=static_cast<std::uint32_t>(crc32(0,
            reinterpret_cast<const Bytef*>(entry.bytes.data()),static_cast<uInt>(entry.bytes.size())));
        put32(output,0x04034b50);put16(output,20);put16(output,0x0800);
        put16(output,method);put16(output,0);put16(output,0);
        put32(output,crc);put32(output,static_cast<std::uint32_t>(payload.size()));
        put32(output,static_cast<std::uint32_t>(entry.bytes.size()));
        put16(output,static_cast<std::uint16_t>(path.size()));put16(output,0);
        output+=path;output+=payload;

        put32(central,0x02014b50);put16(central,20);put16(central,20);
        put16(central,0x0800);put16(central,method);put16(central,0);put16(central,0);
        put32(central,crc);put32(central,static_cast<std::uint32_t>(payload.size()));
        put32(central,static_cast<std::uint32_t>(entry.bytes.size()));
        put16(central,static_cast<std::uint16_t>(path.size()));put16(central,0);
        put16(central,0);put16(central,0);put16(central,0);put32(central,0);
        put32(central,offset);central+=path;
    }
    require(central.size()<=std::numeric_limits<std::uint32_t>::max()&&
            output.size()+central.size()+22<=maxInput);
    const auto start=static_cast<std::uint32_t>(output.size());
    output+=central;
    put32(output,0x06054b50);put16(output,0);put16(output,0);
    put16(output,static_cast<std::uint16_t>(archive.entries.size()));
    put16(output,static_cast<std::uint16_t>(archive.entries.size()));
    put32(output,static_cast<std::uint32_t>(central.size()));put32(output,start);
    put16(output,0);
    require(expanded<=std::uint64_t(output.size())*100);
    return output;
}
}
