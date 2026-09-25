#include <pandoeditor/giszip.h>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace pandoeditor;

namespace {
std::uint16_t u16(const std::string& s,std::size_t i) {
    return static_cast<unsigned char>(s.at(i))|(static_cast<unsigned char>(s.at(i+1))<<8);
}
std::uint32_t u32(const std::string& s,std::size_t i) {
    return u16(s,i)|(static_cast<std::uint32_t>(u16(s,i+2))<<16);
}
void put16(std::string& s,std::size_t i,std::uint16_t n) {
    s.at(i)=char(n);s.at(i+1)=char(n>>8);
}
void put32(std::string& s,std::size_t i,std::uint32_t n) {
    put16(s,i,n&0xffff);put16(s,i+2,n>>16);
}
bool rejected(const std::function<void()>& f) {
    try { f(); } catch(const std::invalid_argument&) { return true; }
    return false;
}
}

int main() {
    std::ifstream input(WEB_GIS_ZIP_FIXTURE,std::ios::binary);
    assert(input);
    const std::string original((std::istreambuf_iterator<char>(input)),{});
    const auto archive=readGisZipArchive(original);
    assert(archive.entries.size()==6);
    assert(archive.entries.front().path=="countries.geojson");
    assert(archive.entries.front().bytes.find("FeatureCollection")!=std::string::npos);
    assert(archive.entries.back().path=="manifest.json");
    assert(archive.entries.back().bytes.find("\"schemaVersion\": 3")!=std::string::npos);
    auto corrupt=original;
    corrupt.resize(corrupt.size()-5);
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    const auto central=original.find("PK\001\002");
    assert(central!=std::string::npos);
    corrupt=original;corrupt.at(central)='X';
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    corrupt=original;corrupt.at(central+16)^=1; // central CRC
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    corrupt=original;put16(corrupt,central+8,u16(corrupt,central+8)|1); // encryption
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    corrupt=original;put16(corrupt,central+10,99); // unsupported compression
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    corrupt=original;put32(corrupt,central+24,0xffffffff); // ZIP64 / zip bomb
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    corrupt=original; // absolute archive path
    corrupt.at(central+46)='/';
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    const auto eocd=original.rfind("PK\005\006");
    assert(eocd!=std::string::npos);
    corrupt=original;put16(corrupt,eocd+10,10001); // entry count
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    corrupt=original;put32(corrupt,eocd+16,u32(corrupt,eocd+16)+1); // central offset
    assert(rejected([&]{readGisZipArchive(corrupt);}));
    assert(rejected([]{readGisZipArchive("");}));
}
