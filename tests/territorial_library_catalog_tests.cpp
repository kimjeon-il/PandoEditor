#include "territoriallibrarycatalog.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <zlib.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Catalog=pandoeditor::TerritorialLibraryCatalog;
const QByteArray indexPin="63c072095fd6c95034365f6d7f9d4e4ff99f71684890bffc8e678fe741b4334c";
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message) {
    bool rejected=false;try {action();}catch(const std::exception&){rejected=true;}require(rejected,message);
}
QByteArray read(const QString& path) {QFile file(path);require(file.open(QIODevice::ReadOnly),"fixture readable");return file.readAll();}
QByteArray hash(const QByteArray& bytes) {return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();}
QByteArray unzip(const QByteArray& input,qint64 expected) {
    QByteArray output(qsizetype(expected+1),Qt::Uninitialized);z_stream stream{};
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));stream.avail_in=uInt(input.size());
    stream.next_out=reinterpret_cast<Bytef*>(output.data());stream.avail_out=uInt(output.size());
    require(inflateInit2(&stream,MAX_WBITS+16)==Z_OK,"negative fixture unzip init");
    const int status=inflate(&stream,Z_FINISH);const auto size=stream.total_out;inflateEnd(&stream);
    require(status==Z_STREAM_END&&size==uLong(expected),"fixed negative fixture unzip");output.resize(qsizetype(size));return output;
}
QByteArray zip(const QByteArray& input) {
    z_stream stream{};require(deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY)==Z_OK,"negative fixture zip init");
    QByteArray output(qsizetype(deflateBound(&stream,uLong(input.size()))),Qt::Uninitialized);
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));stream.avail_in=uInt(input.size());
    stream.next_out=reinterpret_cast<Bytef*>(output.data());stream.avail_out=uInt(output.size());
    const int status=deflate(&stream,Z_FINISH);const auto size=stream.total_out;deflateEnd(&stream);
    require(status==Z_STREAM_END,"negative fixture zip complete");output.resize(qsizetype(size));return output;
}
QStringList searchedIds(const QJsonArray& groups) {
    QStringList result;for(const auto& group:groups)for(const auto& entity:group.toObject().value("entities").toArray())
        result.push_back(entity.toObject().value("entityId").toString());return result;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);int passed=0,failed=0;
    const auto test=[&](const std::string& name,const std::function<void()>& body) {
        try {body();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    try {
        require(argc==2,"fixed v2 asset directory required");const QString root=QString::fromLocal8Bit(argv[1]);
        const QByteArray index=read(QDir(root).filePath("index.json"));
        require(index.size()==555560&&hash(index)==indexPin,"fixed ebc index fixture identity before behavior tests");
        const auto catalog=[&](Catalog::Reader reader={}) {return std::make_unique<Catalog>(index,indexPin,root,std::move(reader));};
        // These literals come from immutable ebc source semantics and catalog,
        // never from native output. Changing to old pilot, nearest-date lookup,
        // eager chunk reads, weak integrity or mutable cached clones breaks them.
        test("exact latest catalog has284entities262lineages2snapshots without geometry",[&] {
            const auto value=catalog();require(value->entries().size()==284,"latest entities");
            require(value->lineages().size()==262&&value->snapshots().size()==2,"lineages and snapshots");
            require(value->loadedEntityCount()==0,"metadata load is lazy");
        });
        test("case-insensitive lineage search preserves Korean names and order",[&] {
            const auto value=catalog();
            require(searchedIds(value->search("kOrEa","2026-10-06"))==QStringList{"state:KOR","state:PRK"},"lineage name match");
            require(value->entry("state:KOR").value("names").toObject().value("ko")==QStringLiteral("대한민국"),"current source names schema");
            require(searchedIds(value->search("대한민국","2026"))==QStringList{"state:KOR"},"entity-specific name match");
        });
        test("search by lineage includes alive historical entities at exact date",[&] {
            const auto value=catalog();
            require(searchedIds(value->search("Germany","1900-01-01"))==
                QStringList{"state:DEU","state:east-prussia","state:west-prussia"},"lineage historical search");
        });
        test("alive DDR remains searchable with explicit null geometry gap",[&] {
            const auto value=catalog();const auto result=value->search("독일 민주공화국","1989-04-24");
            require(searchedIds(result)==QStringList{"state:deutsche-demokratische-republik"},"alive entity remains in results");
            require(result[0].toObject().value("entities").toArray()[0].toObject().value("selectedVersionId").isNull(),"explicit missing version");
            require(value->selectedVersionId("state:deutsche-demokratische-republik","1989-04-24").isEmpty(),"no nearest version");
        });
        for(const auto& row:std::vector<std::pair<QString,QString>>{
            {"1941-04-17","state:kingdom-of-yugoslavia:1918-1941-r1"},{"1943",""},
            {"1945-11-29","state:sfr-yugoslavia:1945-1992-r1"},{"1992-04-26","state:sfr-yugoslavia:1945-1992-r1"},
            {"1992-04-27","state:federal-republic-of-yugoslavia:1992-2003-r1"},
            {"1992-04","state:federal-republic-of-yugoslavia:1992-2003-r1"},{"2003",""}})
            test("literal inclusive/end-precision Yugoslavia "+row.first.toStdString(),[&] {
                const auto value=catalog();require(value->selectedVersionId("state:yugoslavia",row.first)==row.second,"literal selected version or gap");
            });
        test("point-only DDR boundary rejects coarse year-end preview",[&] {
            const auto value=catalog();
            require(!value->selectedVersionId("state:deutsche-demokratische-republik","1989-04-25").isEmpty(),"exact supplied date");
            require(value->selectedVersionId("state:deutsche-demokratische-republik","1989").isEmpty(),"coarse cursor is year end");
            rejects([&]{value->preview("state:deutsche-demokratische-republik","1989");},"gap preview disabled");
            rejects([&]{value->instantiateDescriptors({"state:yugoslavia"},"1943");},"gap instantiation disabled");
        });
        test("search and version inspection never read compressed geometry",[&] {
            int reads=0;const auto value=catalog([&](const QString& file){++reads;return read(QDir(root).filePath(file));});
            value->search("","1991-06-01");value->selectedVersionId("state:KOR","2026");
            require(reads==0&&value->loadedEntityCount()==0,"metadata-only lookup has no eager chunks");
            const auto first=value->loadEntity("state:KOR");const auto second=value->loadEntity("state:KOR");
            require(reads==1&&value->loadedEntityCount()==1&&first==second,"one lazy verified source cached");
            require(first.value("geometryVersions").toArray()[0].toObject().value("geometry").isObject(),"real gzip geometry decoded");
        });
        test("descriptor clone preserves source provenance and independent lifetime",[&] {
            const auto value=catalog();auto result=value->instantiateDescriptors({"state:ABW"},"2026-10-06");
            require(result.size()==1,"exact requested entity only");auto descriptor=result[0].toObject();
            require(descriptor.value("entityId")==QStringLiteral("state:ABW")&&descriptor.value("parentEntityId")==QStringLiteral("state:NLD"),"source entity and source parent identities");
            require(descriptor.value("name")==QStringLiteral("아루바")&&descriptor.value("validFrom").isNull()&&descriptor.value("validTo").isNull(),"fresh instance has no source lifetime restriction");
            const auto metadata=descriptor.value("metadata").toObject();
            require(metadata.value("sourceReferenceDate")==QStringLiteral("2026-10-06")&&metadata.value("sourceInfo").isObject(),"provenance copied");
            require(descriptor.value("geometry").isObject()&&descriptor.value("instantiation").toObject().value("mode")==QStringLiteral("independent"),"direct verified boundary and mode");
            descriptor["geometry"]=QJsonObject{};descriptor["name"]="mutated";
            require(value->instantiateDescriptors({"state:ABW"},"2026-10-06")[0].toObject().value("name")==QStringLiteral("아루바"),"mutating returned descriptor cannot change cached catalog");
        });
        test("living children follow current catalog hierarchy without synthesized ancestors",[&] {
            const auto value=catalog();const auto refs=value->entityRefsWithChildren({"state:soviet-union"},"1991-06-01","all");
            require(refs.size()==16&&refs.front()==QStringLiteral("state:soviet-union"),"root plus15 living constituent republics");
            require(value->entityRefsWithChildren({"state:soviet-union"},"1991","all")==QStringList{"state:soviet-union"},"coarse year end excludes Dec25 children");
            require(value->entityRefsWithChildren({"state:ABW"},"2026","none")==QStringList{"state:ABW"},"parent isn't synthesized");
        });
        test("invalid dates fail visibly in search and instantiation",[&] {
            const auto value=catalog();for(const QString& date:{QString("0000"),QString("1900-02-29"),QString("10000"),QString("")}) {
                rejects([&]{value->search("",date);},"invalid date search rejected");
                rejects([&]{value->instantiateDescriptors({"state:KOR"},date);},"invalid reference date rejected");
            }
        });
        test("index stored hash and duplicate JSON keys are rejected",[&] {
            rejects([&]{Catalog wrong(index,QByteArray(64,'0'),root);},"mandatory index pin");
            const QByteArray duplicate="{\"schemaVersion\":2,\"schemaVersion\":2,\"entities\":[],\"lineages\":[],\"snapshots\":[]}";
            rejects([&]{Catalog wrong(duplicate,hash(duplicate),root);},"duplicate keys cannot silently replace metadata");
        });
        for(const auto& row:std::vector<std::pair<QString,QString>>{{"-0001","bce"},{"0001","ce"},
            {"2000-02-28","ce"},{"2000-02-29",""},{"2000-02",""},{"+010000","extended"},{"+009999",""}})
            test("independent synthetic BCE/extended/month-end "+row.first.toStdString(),[&] {
                auto synthetic=QJsonDocument::fromJson(index).object();
                auto entity=synthetic.value("entities").toArray()[0].toObject();
                entity["entityId"]="state:sample";entity["lineageId"]="sample";entity["file"]="state-sample.json.gz";
                entity["parentEntityId"]="";entity["names"]=QJsonObject{{"ko","제국"}};
                entity["geometryVersions"]=QJsonArray{
                    QJsonObject{{"versionId","bce"},{"validFrom","-0400"},{"validTo","-0001"}},
                    QJsonObject{{"versionId","ce"},{"validFrom","0001"},{"validTo","2000-02-28"}},
                    QJsonObject{{"versionId","extended"},{"validFrom","+010000"},{"validTo",QJsonValue::Null}}};
                entity["geometryVersionCount"]=3;
                synthetic["entities"]=QJsonArray{entity};
                synthetic["lineages"]=QJsonArray{QJsonObject{{"schemaVersion",1},{"lineageId","sample"},
                    {"names",QJsonObject{{"ko","제국"}}},{"relations",QJsonArray{}},{"entityRefs",QJsonArray{"state:sample"}}}};
                synthetic["snapshots"]=QJsonArray{};
                const auto bytes=QJsonDocument(synthetic).toJson();Catalog value(bytes,hash(bytes),root);
                require(value.selectedVersionId("state:sample",row.first)==row.second,"literal temporal selection retains supplied precision");
            });
        test("catalog duplicate identities, cyclic parents, membership holes and overlap reject",[&] {
            const auto original=QJsonDocument::fromJson(index).object();
            for(int mutation=0;mutation<4;++mutation) {
                auto candidate=original;auto entities=candidate.value("entities").toArray();auto first=entities[0].toObject();
                if(mutation==0)entities.push_back(first);
                if(mutation==1){first["parentEntityId"]=first.value("entityId");entities[0]=first;}
                if(mutation==2)candidate["lineages"]=QJsonArray{};
                if(mutation==3){auto versions=first.value("geometryVersions").toArray();auto extra=versions[0].toObject();
                    extra["versionId"]="overlap";versions.push_back(extra);first["geometryVersions"]=versions;
                    first["geometryVersionCount"]=2;entities[0]=first;}
                candidate["entities"]=entities;const auto bytes=QJsonDocument(candidate).toJson();
                rejects([&]{Catalog wrong(bytes,hash(bytes),root);},"invalid graph or temporal catalog rejected");
            }
        });
        test("stored chunk corruption never publishes cached entity",[&] {
            const auto value=catalog([&](const QString& file){auto bytes=read(QDir(root).filePath(file));
                const auto offset=bytes.size()/2;bytes[offset]=char(bytes.at(offset)^1);return bytes;});
            rejects([&]{value->loadEntity("state:KOR");},"gzip stored SHA mismatch rejects");
            require(value->loadedEntityCount()==0,"failed integrity has no cache insertion");
        });
        // Deliberately rehash malformed input so these cases reach decoding and
        // semantic validation instead of stopping at the stored SHA check.
        for(const std::string& mutation:{"gzip-crc","gzip-trailing","decoded-length","metadata-identity","invalid-geometry","duplicate-json","invalid-utf8"})
            test("rehash cannot bypass verified chunk boundary "+mutation,[&] {
                auto candidate=QJsonDocument::fromJson(index).object();auto entities=candidate.value("entities").toArray();
                const auto fixed=entities[0].toObject();require(fixed.value("entityId")==QStringLiteral("state:ABW"),"independent ABW negative fixture source");
                auto stored=read(QDir(root).filePath(fixed.value("file").toString()));
                auto decoded=unzip(stored,qint64(fixed.value("decodedBytes").toDouble()));
                auto changed=fixed;
                if(mutation=="gzip-crc")stored[stored.size()-8]=char(stored.at(stored.size()-8)^1);
                else if(mutation=="gzip-trailing")stored.append('x');
                else if(mutation=="decoded-length")changed["decodedBytes"]=fixed.value("decodedBytes").toDouble()+1;
                else {
                    if(mutation=="duplicate-json")decoded.insert(1,"\"schemaVersion\":2,");
                    else if(mutation=="invalid-utf8") {
                        const auto position=decoded.indexOf("아루바");require(position>=0,"fixed localized UTF-8 token");decoded[position]=char(0xff);
                    } else {
                        auto chunk=QJsonDocument::fromJson(decoded).object();
                        if(mutation=="metadata-identity")chunk["entityId"]="state:KOR";
                        else {auto versions=chunk.value("geometryVersions").toArray();auto version=versions[0].toObject();
                            version["geometry"]=QJsonObject{{"type","Polygon"},{"coordinates",QJsonArray{QJsonArray{
                                QJsonArray{0,0},QJsonArray{1,0},QJsonArray{0,1},QJsonArray{2,2}}}}};
                            versions[0]=version;chunk["geometryVersions"]=versions;}
                        decoded=QJsonDocument(chunk).toJson(QJsonDocument::Compact);
                    }
                    stored=zip(decoded);changed["decodedBytes"]=double(decoded.size());
                }
                changed["compressedBytes"]=double(stored.size());changed["sha256"]=QString::fromLatin1(hash(stored));
                entities[0]=changed;candidate["entities"]=entities;const auto bytes=QJsonDocument(candidate).toJson();
                Catalog value(bytes,hash(bytes),root,[&](const QString&){return stored;});
                rejects([&]{value.loadEntity("state:ABW");},"malformed but correctly hashed input rejected");
                require(value.loadedEntityCount()==0,"failure never publishes malformed cache entry");
            });
        test("failed lazy load can retry verified original without poisoned cache",[&] {
            int reads=0;const auto value=catalog([&](const QString& file){auto bytes=read(QDir(root).filePath(file));
                if(++reads==1)bytes[bytes.size()/2]=char(bytes.at(bytes.size()/2)^1);return bytes;});
            rejects([&]{value->loadEntity("state:KOR");},"first corrupt input rejected");
            require(value->loadedEntityCount()==0,"failed first request absent");
            require(value->loadEntity("state:KOR").value("entityId")==QStringLiteral("state:KOR"),"subsequent source retry succeeds");
            value->loadEntity("state:KOR");require(reads==2&&value->loadedEntityCount()==1,"only verified retry cached");
        });
        test("all284 immutable production gzip chunks validate through native reader",[&] {
            const auto value=catalog();for(const auto& raw:value->entries()) {
                const auto entry=raw.toObject();const auto loaded=value->loadEntity(entry.value("entityId").toString());
                require(loaded.value("entityId")==entry.value("entityId"),"indexed and decoded exact source identity");
                for(const auto& version:loaded.value("geometryVersions").toArray())require(version.toObject().value("geometry").isObject(),"each real boundary structurally decoded");
            }
            require(value->loadedEntityCount()==284,"all fixed latest chunks validated");
        });
        test("unknown entity and unsupported child depth fail visibly",[&] {
            const auto value=catalog();rejects([&]{value->loadEntity("state:absent");},"unknown identity");
            rejects([&]{value->entityRefsWithChildren({"state:KOR"},"2026","nearest");},"unknown hierarchy mode");
        });
        std::cout<<passed<<" passed; "<<failed<<" failed; skip=0\n";return failed?1:0;
    }catch(const std::exception& error){std::cerr<<"FAIL setup: "<<error.what()<<'\n';return 1;}
}
