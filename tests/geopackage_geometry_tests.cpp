#include <pandoeditor/geopackagegeometry.h>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
namespace {
bool rejected(const std::function<void()>& fn) {
    try {fn();}catch(const std::invalid_argument&){return true;}
    return false;
}
Geometry polygon() {
    Geometry g;g.type="Polygon";
    g.polygons={Polygon{Ring{{0,0},{4,0},{4,4},{0,4},{0,0}},
                        Ring{{1,1},{2,1},{2,2},{1,2},{1,1}}}};
    return g;
}
}
int main() {
    auto bytes=encodeGeoPackageGeometry(polygon());
    assert(bytes.size()>8);
    assert(bytes[0]=='G'&&bytes[1]=='P'&&bytes[2]==0&&bytes[3]==1);
    assert(bytes[4]==0xe6&&bytes[5]==0x10&&bytes[6]==0&&bytes[7]==0);
    assert(bytes[8]==1&&bytes[9]==3&&bytes[10]==0);
    auto decoded=decodeGeoPackageGeometry(bytes);
    assert(decoded.type=="Polygon"&&decoded.polygons.front().size()==2);
    assert(decoded.polygons.front()[1][2].x==2);
    Geometry multiple=polygon();multiple.type="MultiPolygon";
    multiple.polygons.push_back(Polygon{Ring{{10,0},{11,0},{11,1},{10,1},{10,0}}});
    decoded=decodeGeoPackageGeometry(encodeGeoPackageGeometry(multiple));
    assert(decoded.type=="MultiPolygon"&&decoded.polygons.size()==2);
    auto corrupt=bytes;corrupt[0]='X';assert(rejected([&]{decodeGeoPackageGeometry(corrupt);}));
    corrupt=bytes;corrupt[2]=1;assert(rejected([&]{decodeGeoPackageGeometry(corrupt);}));
    corrupt=bytes;corrupt[3]=0x21;assert(rejected([&]{decodeGeoPackageGeometry(corrupt);}));
    corrupt=bytes;corrupt[4]=0x11;assert(rejected([&]{decodeGeoPackageGeometry(corrupt);}));
    corrupt=bytes;corrupt.pop_back();assert(rejected([&]{decodeGeoPackageGeometry(corrupt);}));
    corrupt=bytes;corrupt.push_back(0);assert(rejected([&]{decodeGeoPackageGeometry(corrupt);}));
    corrupt=bytes;corrupt[12]=0xff;corrupt[13]=0xff;corrupt[14]=0xff;corrupt[15]=0xff;
    assert(rejected([&]{decodeGeoPackageGeometry(corrupt);}));
}
