#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace pandoeditor {
struct TemporalKey {
    int year=0, month=1, day=1;
    bool operator==(const TemporalKey& b) const {
        return std::tie(year,month,day)==std::tie(b.year,b.month,b.day);
    }
    bool operator<(const TemporalKey& b) const {
        return std::tie(year,month,day)<std::tie(b.year,b.month,b.day);
    }
};
struct TemporalValue {
    std::string text, precision;
    std::int64_t start=0, end=0; // Existing YYYYMMDD public keys.
    std::string canonical;
    int year=0;
    std::optional<int> month, day;
    TemporalKey startKey, endKey;
};
enum class TemporalBoundary { Start, End };
struct TemporalInterval {
    std::optional<std::string> validFrom, validTo;
    std::optional<TemporalValue> start, end;
};

TemporalValue parseTemporal(const std::string&);
std::optional<std::string> normalizeTemporal(const std::optional<std::string>&);
int compareTemporal(const TemporalValue&,const TemporalValue&,
                    TemporalBoundary left=TemporalBoundary::Start,
                    TemporalBoundary right=TemporalBoundary::Start);
TemporalInterval normalizeTemporalInterval(const std::optional<std::string>& validFrom,
                                           const std::optional<std::string>& validTo);
bool temporalContains(const TemporalInterval&,const TemporalValue&);
bool temporalIntervalsOverlap(const TemporalInterval&,const TemporalInterval&);
}
