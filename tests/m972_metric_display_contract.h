#pragma once
#include "riverareacalculator.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QByteArray>
#include <QString>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJSEngine>
#include <QFile>
#include <QCryptographicHash>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QRegularExpression>
#include <future>
#include <memory>
#include <cstring>
#include <cmath>
#include <stdexcept>
namespace pandoeditor::m972test {
struct MetricDisplayContractResult {
    bool accepted=false,rawScalarEqual=false;
    QString detail,nativeFormattedText,browserFormattedText,nativeFullText;
    double nativeAreaKm2=0,browserAreaKm2=0;
    QString nativeFloat64,browserFloat64,ulpDistance,mapViewSourceSha256,inputSha256,normalizedInputSha256;
    QJsonObject toJson() const {return {{"accepted",accepted},{"detail",detail},{"nativeFormattedText",nativeFormattedText},{"browserFormattedText",browserFormattedText},{"nativeFullText",nativeFullText},{"nativeAreaKm2",nativeAreaKm2},{"browserAreaKm2",browserAreaKm2},{"nativeFloat64",nativeFloat64},{"browserFloat64",browserFloat64},{"ulpDistance",ulpDistance},{"rawScalarEqual",rawScalarEqual},{"mapViewSourceSha256",mapViewSourceSha256},{"inputSha256",inputSha256},{"normalizedInputSha256",normalizedInputSha256}};}
};
struct ProductionMetricFormatterResult {bool ok=false;QString detail,text,fullText,sourceSha256;};
namespace metric_detail {
inline QString sha256(const QByteArray& bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
inline quint64 bits(double value) {quint64 result;std::memcpy(&result,&value,sizeof result);return result;}
inline QString hex(double value) {return QString::number(bits(value),16).rightJustified(16,'0');}
inline quint64 orderedBits(quint64 value) {constexpr quint64 sign=quint64(1)<<63;return value&sign?~value:value|sign;}
inline double scalar(const QJsonValue& value,const char* field) {
    if(!value.isDouble()||!std::isfinite(value.toDouble())||value.toDouble()<0)
        throw std::runtime_error((QString("METRIC_INVALID_SCALAR: ")+field).toStdString());
    return value.toDouble();
}
inline void require(bool condition,const char* detail) {if(!condition)throw std::runtime_error(detail);}
inline QJsonObject geometryJson(const Geometry& geometry) {
    require(geometry.type=="Polygon"||geometry.type=="MultiPolygon","METRIC_INVALID_TRANSFER_TYPE");
    QJsonArray polygons;
    for(const auto& polygon:geometry.polygons){QJsonArray rings;for(const auto& ring:polygon){QJsonArray points;
        for(const auto& point:ring){require(std::isfinite(point.x)&&std::isfinite(point.y),"METRIC_INVALID_TRANSFER_POINT");points.append(QJsonArray{point.x,point.y});}
        rings.append(points);}polygons.append(rings);}
    require(geometry.type!="Polygon"||polygons.size()==1,"METRIC_INVALID_POLYGON_SHAPE");
    return {{"coordinates",geometry.type=="Polygon"?polygons[0]:QJsonValue(polygons)},{"type",QString::fromStdString(geometry.type)}};
}
inline QString canonicalHash(QJSEngine& engine,const QJsonObject& geometry) {
    // These owned GeoJSON values have only coordinates/type keys. Qt orders
    // object keys; JS JSON.stringify supplies the exact web number encoding.
    const auto parsed=engine.globalObject().property("JSON").property("parse").call({QString::fromUtf8(QJsonDocument(geometry).toJson(QJsonDocument::Compact))});
    require(!parsed.isError(),"METRIC_INPUT_JSON_PARSE_FAILED");
    const auto text=engine.globalObject().property("JSON").property("stringify").call({parsed});
    require(!text.isError()&&text.isString(),"METRIC_INPUT_JSON_STRINGIFY_FAILED");return sha256(text.toString().toUtf8());
}
}
inline QByteArray loadProductionMapViewSource() {
    QFile file(":/metric/common/MapView.qml");if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();
}
inline ProductionMetricFormatterResult evaluateProductionMetricFormatter(const QJsonValue& area,const QByteArray& productionSource) {
    ProductionMetricFormatterResult result;
    try {
        metric_detail::scalar(area,"native formatter area");metric_detail::require(!productionSource.isEmpty(),"METRIC_MAP_VIEW_SOURCE_MISSING");
        const auto source=QString::fromUtf8(productionSource);const QString marker="objectName:\"geometryTransferMetrics\"";
        metric_detail::require(source.count(marker)==1,"METRIC_FORMATTER_BLOCK_NOT_UNIQUE");const int at=source.indexOf(marker);
        const int start=source.lastIndexOf("Label {",at),end=source.indexOf("\n                    }",at);
        metric_detail::require(start>=0&&end>at,"METRIC_FORMATTER_BLOCK_UNSUPPORTED");
        QStringList lines;for(const auto& line:source.mid(start,end-start).split('\n'))if(!line.trimmed().isEmpty())lines.append(line.trimmed());
        metric_detail::require(lines.size()==7&&lines[0]=="Label {"&&lines[1].startsWith(marker+";")&&lines[2].startsWith("visible:")&&
            lines[3].startsWith("readonly property real areaKm2:")&&lines[4].startsWith("readonly property int fractionDigits:")&&
            lines[5].startsWith("text:")&&lines[6].startsWith("color:"),"METRIC_FORMATTER_BLOCK_UNSUPPORTED");
        const QString caption=QStringLiteral("편입 면적: ");
        metric_detail::require(lines[5].startsWith("text:\""+caption+"\"+"),"METRIC_FORMATTER_CAPTION_UNSUPPORTED");
        const QJsonObject state{{"taskState",QJsonObject{{"transferAreaKm2",area}}}};
        const auto qml=QString("import QtQml\nQtObject {\nproperty var taskPanel: (%1)\n%2\n%3\nreadonly property string renderedLabel:%4\n}\n")
            .arg(QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)),lines[3],lines[4],lines[5].mid(5));
        QQmlEngine engine;QQmlComponent component(&engine);component.setData(qml.toUtf8(),QUrl("qrc:/metric/common/MapView.qml#geometryTransferMetrics"));
        metric_detail::require(component.isReady(),("METRIC_FORMATTER_QML_FAILED: "+component.errorString()).toUtf8().constData());
        std::unique_ptr<QObject> object(component.create());metric_detail::require(bool(object),"METRIC_FORMATTER_QML_OBJECT_FAILED");
        result.fullText=object->property("renderedLabel").toString();metric_detail::require(result.fullText.startsWith(caption),"METRIC_FORMATTER_CAPTION_CHANGED");
        result.text=result.fullText.mid(caption.size());metric_detail::require(!result.text.isEmpty(),"METRIC_FORMATTER_TEXT_MISSING");
        result.sourceSha256=metric_detail::sha256(productionSource);result.ok=true;
    }catch(const std::exception& error){result.detail=QString::fromUtf8(error.what());}
    return result;
}
inline MetricDisplayContractResult validateMetricDisplayContract(const QJsonObject& diagnostic,const Geometry& exactNativeTransfer,
    const QJsonValue& capturedNativeArea,const QJsonValue& browserCanonicalArea,const QJsonObject& browserSourceHashes,
    const QByteArray& productionMapViewSource,const GeometryCancellation& cancelled = {}) {
    MetricDisplayContractResult result;
    try {
        const auto check=[&]{metric_detail::require(!(cancelled&&cancelled()),"METRIC_CANCELLED");};check();
        result.nativeAreaKm2=metric_detail::scalar(capturedNativeArea,"captured native area");result.browserAreaKm2=metric_detail::scalar(browserCanonicalArea,"canonical browser area");
        // Diagnostics describe the actual raw values even when later identity or
        // formatted-text validation fails. They never imply display acceptance.
        result.nativeFloat64=metric_detail::hex(result.nativeAreaKm2);result.browserFloat64=metric_detail::hex(result.browserAreaKm2);
        const auto nativeBits=metric_detail::bits(result.nativeAreaKm2),browserBits=metric_detail::bits(result.browserAreaKm2);
        const auto orderedNative=metric_detail::orderedBits(nativeBits),orderedBrowser=metric_detail::orderedBits(browserBits);
        result.ulpDistance=QString::number(orderedNative>=orderedBrowser?orderedNative-orderedBrowser:orderedBrowser-orderedNative);
        result.rawScalarEqual=nativeBits==browserBits;
        const QJsonObject pins{{"controller/d3.min.js","4cdf92091ed0cfdd8b862af1c6d4744bd0458e746b92c1bbd5403a1143ecd538"},
            {"controller/polygon-geometry.js","cc987c4076861a02a5d60720ebf536908175a9f1a605a86701ae4cf50c3a3fb5"},
            {"controller/app-territory-components.js","c82290d2e3601a4d07b6d94746a665bfad9a49c1020f447a0ace90740ef226d9"}};
        for(auto it=pins.begin();it!=pins.end();++it)metric_detail::require(browserSourceHashes[it.key()]==it.value(),"METRIC_BROWSER_SOURCE_IDENTITY_MISMATCH");
        metric_detail::require(diagnostic["sourceIdentity"].isObject(),"METRIC_DIAGNOSTIC_SOURCE_IDENTITY_MISSING");const auto sourceIdentity=diagnostic["sourceIdentity"].toObject();
        metric_detail::require(sourceIdentity["d3Sha256"]==pins["controller/d3.min.js"]&&sourceIdentity["normalizerSha256"]==pins["controller/polygon-geometry.js"]&&
            sourceIdentity["formatterSha256"]==pins["controller/app-territory-components.js"]&&sourceIdentity["formatterEntrypoint"]=="app-territory-components.formatTerritoryArea","METRIC_DIAGNOSTIC_SOURCE_IDENTITY_MISMATCH");
        // Same-runtime measurement is exact, and stays on a private worker.
        // No captured value can be rescued by a copied/forged formatted label.
        const auto measured=std::async(std::launch::async,[exactNativeTransfer,cancelled] {
            auto normalized=normalizeRiverGeometry(exactNativeTransfer,cancelled);RiverAreaResult area;
            if(normalized.succeeded()&&normalized.geometry)area=calculateRiverAreaKm2(*normalized.geometry,cancelled);
            return std::make_pair(std::move(normalized),std::move(area));
        }).get();check();
        metric_detail::require(measured.first.succeeded()&&measured.first.geometry&&measured.second.succeeded(),"METRIC_NATIVE_DIRECT_MEASUREMENT_FAILED");
        metric_detail::require(metric_detail::bits(measured.second.areaKm2)==metric_detail::bits(result.nativeAreaKm2),"METRIC_NATIVE_CAPTURE_DOES_NOT_REPRODUCE_DIRECT_D3");
        QJSEngine jsonEngine;const auto rawGeometry=metric_detail::geometryJson(exactNativeTransfer),normalizedGeometry=metric_detail::geometryJson(*measured.first.geometry);
        result.inputSha256=metric_detail::canonicalHash(jsonEngine,rawGeometry);result.normalizedInputSha256=metric_detail::canonicalHash(jsonEngine,normalizedGeometry);
        metric_detail::require(diagnostic["inputSha256"].isString()&&diagnostic["inputSha256"].toString()==result.inputSha256,"METRIC_RAW_INPUT_HASH_MISMATCH");
        metric_detail::require(diagnostic["normalizedInputSha256"].isString()&&diagnostic["normalizedInputSha256"].toString()==result.normalizedInputSha256,"METRIC_NORMALIZED_INPUT_HASH_MISMATCH");
        metric_detail::require(diagnostic["normalizedGeometry"].isObject()&&diagnostic["normalizedGeometry"].toObject()==normalizedGeometry,"METRIC_NORMALIZED_INPUT_GEOMETRY_MISMATCH");
        const auto multiply=jsonEngine.evaluate("(function(a,b){return a*b;})");const double radiusSquared=jsonEngine.evaluate("6371.0088**2").toNumber();
        for(const auto& name:{"raw","normalized"}) {
            metric_detail::require(diagnostic[name].isObject(),"METRIC_BROWSER_MEASUREMENT_MISSING");const auto row=diagnostic[name].toObject();
            const double steradians=metric_detail::scalar(row["steradians"],"browser steradians"),radius=metric_detail::scalar(row["radiusSquared"],"browser radius squared"),
                product=metric_detail::scalar(row["productKm2"],"browser product"),clamped=metric_detail::scalar(row["clampedKm2"],"browser clamped product");
            metric_detail::require(radius==radiusSquared,"METRIC_RADIUS_SQUARED_MISMATCH");
            const auto directProduct=multiply.call({steradians,radius});metric_detail::require(!directProduct.isError()&&directProduct.toNumber()==product&&clamped==product,"METRIC_BROWSER_PRODUCT_DOES_NOT_REPRODUCE_DIRECT_D3");
            metric_detail::require(row["float64"].isObject(),"METRIC_FLOAT64_EVIDENCE_MISSING");const auto hex=row["float64"].toObject();
            metric_detail::require(hex["steradians"].toString()==metric_detail::hex(steradians)&&hex["radiusSquared"].toString()==metric_detail::hex(radius)&&hex["productKm2"].toString()==metric_detail::hex(product),"METRIC_FLOAT64_EVIDENCE_MISMATCH");
            metric_detail::require(row["formatted"].isString()&&!row["formatted"].toString().isEmpty(),"METRIC_BROWSER_FORMATTER_EVIDENCE_MISSING");
            // A source hash and copied label cannot validate a coherently forged
            // browser scalar. Re-evaluate the actual production QML expression
            // on that scalar too; no magnitude cap or numeric epsilon is used.
            const auto browserFormatted=evaluateProductionMetricFormatter(row["clampedKm2"],productionMapViewSource);
            metric_detail::require(browserFormatted.ok&&browserFormatted.text==row["formatted"].toString(),"METRIC_BROWSER_FORMATTED_TEXT_DOES_NOT_REPRODUCE_PRODUCTION_FORMATTER");
            if(QString::fromLatin1(name)=="raw") {
                metric_detail::require(product==result.browserAreaKm2,"METRIC_BROWSER_CAPTURE_DOES_NOT_REPRODUCE_DIRECT_D3");result.browserFormattedText=row["formatted"].toString();
            }else metric_detail::require(row["formatted"].toString()==result.browserFormattedText,"METRIC_BROWSER_NORMALIZED_FORMATTED_TEXT_CHANGED");
        }
        const auto formatted=evaluateProductionMetricFormatter(capturedNativeArea,productionMapViewSource);check();
        metric_detail::require(formatted.ok,formatted.detail.toUtf8().constData());result.nativeFormattedText=formatted.text;result.nativeFullText=formatted.fullText;result.mapViewSourceSha256=formatted.sourceSha256;
        metric_detail::require(result.nativeFormattedText==result.browserFormattedText,"METRIC_PRODUCTION_FORMATTED_TEXT_MISMATCH");
        check();result.accepted=true;
    }catch(const std::exception& error){result.detail=QString::fromUtf8(error.what());}
    return result;
}
}
