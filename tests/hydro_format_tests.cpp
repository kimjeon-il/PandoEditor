#include <pandoeditor/hydroformat.h>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <zlib.h>

#ifndef WEB_HYDRO_FIXTURE
#error missing fixture path
#endif

namespace {
std::vector<std::uint8_t> fixture() {
    std::ifstream file(std::string(WEB_HYDRO_FIXTURE)+"/v0.13.0/index.bin.gz",std::ios::binary);
    assert(file);
    std::vector<std::uint8_t> compressed(std::istreambuf_iterator<char>{file},{});
    z_stream stream{};
    stream.next_in=compressed.data();stream.avail_in=static_cast<uInt>(compressed.size());
    assert(inflateInit2(&stream,MAX_WBITS+16)==Z_OK);
    std::vector<std::uint8_t> bytes(4096);
    stream.next_out=bytes.data();stream.avail_out=static_cast<uInt>(bytes.size());
    assert(inflate(&stream,Z_FINISH)==Z_STREAM_END);
    bytes.resize(stream.total_out);inflateEnd(&stream);
    return bytes;
}
pandoeditor::HydroIndex decode(const std::vector<std::uint8_t>& bytes) {
    return pandoeditor::decodeHydroIndex({bytes.data(),bytes.size()},{507});
}
bool rejected(const std::vector<std::uint8_t>& bytes) {
    try {decode(bytes);return false;}catch(const std::runtime_error&){return true;}
}
}
int main() {
    auto bytes=fixture();
    auto index=decode(bytes);
    assert(index.tilePacks.size()==5);
    assert(index.logicalPacks.size()==5);
    assert(index.packSpecs.size()==6);
    assert(index.logicalPacks.at(5)==(std::vector<std::uint32_t>{4,5}));
    assert(index.packSpecs.at(5).offset==423);
    for(std::size_t cut=0;cut<bytes.size();cut++){
        auto truncated=bytes;truncated.resize(cut);assert(rejected(truncated));
    }
    bytes[0]^=1;assert(rejected(bytes));bytes[0]^=1;
    bytes[4]=3;assert(rejected(bytes));bytes[4]=4;
    bytes.push_back(0);assert(rejected(bytes));bytes.pop_back();
    bytes[8]=255;assert(rejected(bytes));bytes[8]=5;
    bytes[20]=9;assert(rejected(bytes));bytes[20]=0;
    assert(rejected(std::vector<std::uint8_t>{}));
}
