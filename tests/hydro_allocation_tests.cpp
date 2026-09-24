#include <pandoeditor/hydroformat.h>
#include <zlib.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>
#include <new>
#include <stdexcept>

static thread_local long failAfter=-1;
void* operator new(std::size_t size){
    if(failAfter==0)throw std::bad_alloc();
    if(failAfter>0)--failAfter;
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}

std::vector<std::uint8_t> inflated(const std::string& filename,std::uint32_t offset=0,std::uint32_t length=0){
    std::ifstream file(filename,std::ios::binary);if(!file)throw std::runtime_error("missing fixture");
    file.seekg(offset);
    std::vector<std::uint8_t> compressed;
    if(length){compressed.resize(length);file.read(reinterpret_cast<char*>(compressed.data()),length);
        if(file.gcount()!=length)throw std::runtime_error("short fixture");}
    else compressed.assign(std::istreambuf_iterator<char>{file},{});
    z_stream stream{};stream.next_in=compressed.data();stream.avail_in=static_cast<uInt>(compressed.size());
    if(inflateInit2(&stream,MAX_WBITS+16)!=Z_OK)throw std::runtime_error("gzip init");
    std::vector<std::uint8_t> output(4096);
    stream.next_out=output.data();stream.avail_out=static_cast<uInt>(output.size());
    const int status=inflate(&stream,Z_FINISH);
    output.resize(stream.total_out);inflateEnd(&stream);
    if(status!=Z_STREAM_END)throw std::runtime_error("gzip fixture");
    return output;
}
template<class Job> int injectEveryAllocation(Job job){
    int failures=0;
    for(long position=0;position<2000;position++){
        failAfter=position;
        try{job();failAfter=-1;return failures;}
        catch(const std::bad_alloc&){failAfter=-1;++failures;}
        catch(...){failAfter=-1;throw;}
    }
    throw std::runtime_error("hydro allocation sweep did not finish");
}
int main(){
    try{
        const std::string root=WEB_HYDRO_FIXTURE;
        const auto indexBytes=inflated(root+"/v0.13.0/index.bin.gz");
        auto index=pandoeditor::decodeHydroIndex({indexBytes.data(),indexBytes.size()},{507});
        const auto spec=index.packSpecs.at(0);
        const auto packBytes=inflated(root+"/v0.13.0/shards/s0.bin",spec.offset,spec.length);
        const std::map<std::uint32_t,std::uint32_t> ids{{1,1},{2,2},{3,3},{4,4},{5,5},{6,5}};
        const auto pack=pandoeditor::decodeHydroPack({packBytes.data(),packBytes.size()},0,ids);
        const auto indexFailures=injectEveryAllocation([&]{
            const auto parsed=pandoeditor::decodeHydroIndex({indexBytes.data(),indexBytes.size()},{507});
            if(parsed.packSpecs.size()!=6)throw std::runtime_error("index changed");
        });
        const auto packFailures=injectEveryAllocation([&]{
            const auto parsed=pandoeditor::decodeHydroPack({packBytes.data(),packBytes.size()},0,ids);
            if(parsed.features.size()!=1)throw std::runtime_error("pack changed");
        });
        const auto packetFailures=injectEveryAllocation([&]{
            const auto packet=pandoeditor::buildHydroRenderPacket(pack);
            if(packet.rivers.size()!=2)throw std::runtime_error("packet changed");
        });
        const auto mergeFailures=injectEveryAllocation([&]{
            const auto geometry=pandoeditor::mergeHydroLogicalFragments({pack.features.front()});
            if(geometry.lines.size()!=1)throw std::runtime_error("merge changed");
        });
        if(!indexFailures||!packFailures||!packetFailures||!mergeFailures||
           indexBytes.empty()||packBytes.empty()||index.packSpecs.size()!=6)
            throw std::runtime_error("allocation sweep did not exercise each path");
        std::cout<<"Hydro allocation failures: index "<<indexFailures<<", pack "<<packFailures
                 <<", packet "<<packetFailures<<", merge "<<mergeFailures<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
