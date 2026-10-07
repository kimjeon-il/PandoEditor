#include "terrainprovider.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImageWriter>
#include <QFileInfo>
#include <QCryptographicHash>
#include <chrono>
#include <future>
#include <atomic>
#include <thread>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
// The fallback keeps this regression executable against the pre-retry API for
// actual RED evidence. Once present, the real provider implementation is used.
template<class T>auto decodeFailure(T& provider,const QString& path,int)->decltype(provider.isDecodeFailure(path)){return provider.isDecodeFailure(path);}
template<class T>bool decodeFailure(T&,const QString&,long){return false;}
template<class T>auto retryAsset(T& provider,const QString& path,int)->decltype(provider.retryVerifiedAsset(path)){return provider.retryVerifiedAsset(path);}
template<class T>bool retryAsset(T&,const QString&,long){return false;}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        require(argc==2,"fixed raster manifest required");
        QFile manifest(QString::fromLocal8Bit(argv[1]));require(manifest.open(QIODevice::ReadOnly),"read fixed manifest");
        QTemporaryDir root;require(root.isValid(),"isolated lossless source fixtures");
        require(QDir(root.path()).mkpath("terrain/v0.12.6/0"),"create tile directory");
        for(int column=0;column<2;++column) {
            QImage image(32,32,QImage::Format_RGBA8888);image.fill(QColor(140+column,80,30,255));
            image.setPixelColor(31,31,QColor(43,91,182,255));
            QImageWriter writer(root.path()+QString("/terrain/v0.12.6/0/%1-0.webp").arg(column),"webp");
            writer.setQuality(100);require(writer.write(image),"write lossless fixture");
        }
        const auto manifestBytes=manifest.readAll();
        TerrainTileProvider provider(manifestBytes,root.path());require(provider.available(),"fixed raster source");
        MapViewState view;view.viewportWidth=2000;view.viewportHeight=1000;view.scale=100;
        view.translateX=1000;view.translateY=500;
        const auto plan=provider.planForView(view);require(plan.baseTiles.size()==2,"canonical complete-world reserve");
        const auto& previous=plan.baseTiles[0];const auto& replacement=plan.baseTiles[1];
        require(provider.expectedDecodedTileBytes(previous)>0,"reservation uses manifest dimensions");
        provider.protectRenderResources({previous},{});
        require(!provider.loadTile(previous).isNull(),"prepare old displayed backing");
        provider.setCacheBudget(0);
        provider.protectRenderResources({replacement},{previous});
        require(provider.resourceCacheSnapshot().pendingCount==1,"new CPU source remains pending");
        require(!provider.loadTile(replacement).isNull(),"decode replacement CPU data");
        require(provider.resourceCacheSnapshot().pendingCount==0,"CPU preparation completed");
        require(provider.resourceCacheSnapshot().residentCount==2,"CPU completion cannot retire displayed fallback");
        require(provider.resourceCacheSnapshot().protectionCounts[std::size_t(pandoeditor::ResourceProtection::Fallback)]==1,
                "display owner still holds old fallback pin");
        provider.protectRenderResources({replacement},{}); // Actual display adoption is caller-owned.
        require(provider.resourceCacheSnapshot().residentCount==1,"explicit handoff makes obsolete fallback evictable");
        provider.protectRenderResources({},{});
        require(provider.cachedBytes()==0&&provider.resourceCacheSnapshot().pendingCount==0,
                "released render ownership settles at configured zero budget");
        std::cout<<"render protection: processed=1 passed=1 failed=0 skip=0; CPU stages=2; adoption/release stages=2\n";
        // Synthetic mechanism fixture: actual file SHA verification belongs to
        // a cold read, never view planning or reuse of immutable decoded bytes.
        const auto digest=[](const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray{};return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256);};
        const auto pathFor=[&](const QString& relative){return QDir(root.path()).filePath(relative);};
        const auto approved=digest(previous.path);require(!approved.isEmpty(),"approved synthetic tile bytes");
        QFile approvedOriginal(previous.path);require(approvedOriginal.open(QIODevice::ReadOnly),"retain independent approved source bytes for retry regression");
        const auto approvedOriginalBytes=approvedOriginal.readAll();approvedOriginal.close();
        int verifiedReads=0;
        TerrainTileProvider isolated(manifestBytes,root.path(),[&](const QString& relative){
            ++verifiedReads;const auto path=pathFor(relative);return digest(path)==approved?path:QString{};
        },{},pathFor);
        require(isolated.available()&&verifiedReads==1,"source initialization consults manifest identity once");
        verifiedReads=0; // View/read phases exclude the constructor's manifest check.
        const auto isolatedPlan=isolated.planForView(view);isolated.tilesForView(view);
        isolated.cachedBytes();isolated.decodeError();isolated.resourceCacheSnapshot();
        require(isolatedPlan.baseTiles.size()==2&&verifiedReads==0,"planning/status must not stream verified source bytes");
        const auto& cold=isolatedPlan.baseTiles.front();
        const auto decoded=isolated.loadTile(cold);require(!decoded.isNull()&&verifiedReads==1,"cold tile read must verify actual source bytes exactly once");
        const auto reused=isolated.loadTile(cold);require(!reused.isNull()&&reused.cacheKey()==decoded.cacheKey()&&verifiedReads==1,"immutable decoded cache reuse must not stream source bytes");
        int redirectedReads=0;
        TerrainTileProvider redirected(manifestBytes,root.path(),[&](const QString& relative){
            ++redirectedReads;const auto path=pathFor(relative);return digest(path)==approved?path:QString{};
        },{},[&](const QString& relative){return QDir(root.path()).filePath("logical-cache/"+relative);});
        require(redirected.available()&&redirectedReads==1,"redirected source initialization consults manifest identity once");
        redirectedReads=0;
        const auto redirectedPlan=redirected.planForView(view);
        require(redirectedReads==0&&!QFile::exists(redirectedPlan.baseTiles.front().path),"logical cache identity does not pretend a source file exists");
        const auto external=redirected.loadTile(redirectedPlan.baseTiles.front());
        require(!external.isNull()&&external==decoded&&redirectedReads==1,"verified external bytes may back a different stable planned identity");
        require(redirected.loadTile(redirectedPlan.baseTiles.front()).cacheKey()==external.cacheKey()&&redirectedReads==1,"redirected immutable cache reuse remains hash-free");
        isolated.setCacheBudget(0);
        QFile corrupted(cold.path);require(corrupted.open(QIODevice::WriteOnly|QIODevice::Truncate),"corrupt isolated source after eviction");require(corrupted.write("unverified-corrupt")>0,"write corrupt bytes");corrupted.close();
        require(isolated.loadTile(cold).isNull()&&verifiedReads==2,"cold verified refusal must not fall through to readable corrupt bytes");
        std::cout<<"planning/read split: planningVerifiedReads=0 coldVerifiedReads=1 cacheVerifiedReads=0 corruptRefused=1\n";
        // Actual WebP decode of a bounded synthetic tint, not source pixel parity.
        QFile demManifest(QFileInfo(manifest.fileName()).dir().filePath("../v0.13.3/manifest.json"));
        require(demManifest.open(QIODevice::ReadOnly),"read fixed DEM manifest for decoder concurrency");
        require(QDir(root.path()).mkpath("terrain/v0.13.3"),"create synthetic tint directory");
        QImage tint(4096,2048,QImage::Format_RGBA8888);quint32 random=0x71d392a5;
        for(int y=0;y<tint.height();++y){auto* row=tint.scanLine(y);for(int x=0;x<tint.width();++x){random=random*1664525u+1013904223u;row[x*4]=uchar(random);row[x*4+1]=uchar(random>>8);row[x*4+2]=uchar(random>>16);row[x*4+3]=255;}}
        QImageWriter tintWriter(root.path()+"/terrain/v0.13.3/tint.webp","webp");tintWriter.setQuality(100);require(tintWriter.write(tint),"write lossless decoder workload");tint={};
        std::promise<void> decoderEntered,releaseDecoder,statusEntered;
        auto entered=decoderEntered.get_future();auto release=releaseDecoder.get_future().share();
        auto statusStarted=statusEntered.get_future();
        TerrainTileProvider dem(demManifest.readAll(),root.path(),{},[&]{decoderEntered.set_value();release.wait();});
        require(dem.available(),"fixed DEM source for concurrency");
        const auto decodeStart=std::chrono::steady_clock::now();
        auto decode=std::async(std::launch::async,[&]{return dem.loadTint();});
        // The latch observes the real read phase, so scheduler timing cannot
        // mistake a finished decode for concurrent status access.
        const bool overlapping=entered.wait_for(std::chrono::seconds(5))==std::future_status::ready;
        const auto statusStart=std::chrono::steady_clock::now();
        auto status=std::async(std::launch::async,[&]{statusEntered.set_value();dem.cachedBytes();dem.decodeError();dem.resourceCacheSnapshot();dem.protectRenderResources({},{});});
        statusStarted.wait();
        const bool prompt=status.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
        releaseDecoder.set_value(); // Always release and join before assertions.
        status.get();
        const auto statusMillis=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-statusStart).count();
        const auto image=decode.get();
        const auto decodeMillis=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decodeStart).count();
        std::cout<<"decoder concurrency: actualDecoderEntry="<<overlapping<<" statusReturnedBeforeDecoderRelease="<<prompt<<" statusMillis="<<statusMillis<<" totalDecodeMillis="<<decodeMillis<<'\n';
        require(overlapping,"actual cold decoder workload overlaps status/protection request");
        require(!image.isNull()&&image.size()==QSize(4096,2048),"actual tint decode completes");
        require(prompt,"status/protection must return while actual decoder is paused before read");
        require(dem.resourceCacheSnapshot().residentCount==1&&dem.cachedBytes()==std::size_t(image.sizeInBytes()),"single immutable tint backing accounting");
        // The matching notification clears only a failed logical asset. It is
        // not proof of decodability: the next cold read still uses the verifier.
        const auto relative=isolated.relativeTilePath(cold);
        require(decodeFailure(isolated,relative,0),"failed cold read is identifiable by exact logical asset");
        const auto readsBeforeRetry=verifiedReads;const auto failureBefore=isolated.decodeError();
        require(!retryAsset(isolated,"terrain/v0.12.6/0/1-0.webp",0)&&isolated.decodeError()==failureBefore,
                "unrelated verified asset must not clear the failed source");
        require(retryAsset(isolated,relative,0)&&!decodeFailure(isolated,relative,0)&&isolated.decodeError().isEmpty(),
                "newly verified matching asset allows a fresh decode attempt");
        require(verifiedReads==readsBeforeRetry,"matching retry notification must not stream/hash source bytes");
        require(isolated.loadTile(cold).isNull()&&decodeFailure(isolated,relative,0)&&verifiedReads==readsBeforeRetry+1,
                "a bad replacement remains refused by actual cold verification");
        require(retryAsset(isolated,relative,0),"matching retry can clear the new failure");
        QFile repaired(cold.path);require(repaired.open(QIODevice::WriteOnly|QIODevice::Truncate),"install replacement source");
        require(repaired.write(approvedOriginalBytes)==approvedOriginalBytes.size(),"write complete replacement");repaired.close();
        require(!isolated.loadTile(cold).isNull()&&verifiedReads==readsBeforeRetry+2&&isolated.decodeError().isEmpty(),
                "same provider retries newly approved bytes through actual cold verifier and decoder");
        require(!retryAsset(isolated,"../terrain/v0.12.6/0/0-0.webp",0)&&isolated.error().isEmpty(),
                "retry cannot alter immutable manifest diagnostics or accept path aliases");
        std::cout<<"matching retry: unrelatedRefused=1 noRead=1 badReplacementRefused=1 verifiedReplacementDecoded=1\n";
        std::cout<<"render protection total: processed=4 passed=4 failed=0 skip=0\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"render protection FAIL: "<<error.what()<<'\n';return 1;}
}
