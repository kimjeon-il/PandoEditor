#pragma once
#include <pandoeditor/map/countrymesh.h>
#include <QByteArray>
#include <memory>

std::shared_ptr<const CountryBaseMesh> decodeCountryBaseMesh(const QByteArray& bytes,bool preview);
