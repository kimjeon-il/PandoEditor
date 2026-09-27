#pragma once
#include <pandoeditor/document.h>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <vector>

struct CanonicalCountrySummary {
    std::uint32_t featureCount=0,polygonCount=0,ringCount=0,positionCount=0;
};

class CanonicalCountryStore final {
public:
    explicit CanonicalCountryStore(QByteArray decompressed);
    CanonicalCountrySummary summary() const {return summary_;}
    std::vector<std::string> ids() const;
    pandoeditor::TerritorialUnit materializeUnit(std::size_t index) const;
    pandoeditor::Geometry materializeGeometry(std::size_t index) const;
    bool geometryEquals(std::size_t index,const pandoeditor::Geometry& geometry) const;
private:
    QByteArray bytes_;
    CanonicalCountrySummary summary_;
    std::vector<QJsonObject> metadata_;
    std::vector<std::uint8_t> types_;
    std::vector<std::uint32_t> featurePolygons_,polygonRings_,ringPositions_;
    std::size_t positionsOffset_=0;
    double coordinate(std::size_t index) const;
};
