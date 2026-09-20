#include "referenceimageitem.h"
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <array>

namespace {
ReferenceWarpMode mode(QString value){if(value=="similarity")return ReferenceWarpMode::Similarity;if(value=="affine")return ReferenceWarpMode::Affine;if(value=="projective")return ReferenceWarpMode::Projective;if(value=="tps")return ReferenceWarpMode::ThinPlateSpline;return ReferenceWarpMode::Auto;}
QPainter::CompositionMode composition(const QString& value){if(value=="multiply")return QPainter::CompositionMode_Multiply;if(value=="screen")return QPainter::CompositionMode_Screen;if(value=="difference")return QPainter::CompositionMode_Difference;return QPainter::CompositionMode_SourceOver;}
QPointF gridPoint(const QRectF& rect,int column,int row){return {rect.left()+rect.width()*column/24.,rect.top()+rect.height()*row/16.};}
}
ReferenceImageItem::ReferenceImageItem(QQuickItem* parent):QQuickPaintedItem(parent){setAntialiasing(true);connect(this,&ReferenceImageItem::changed,this,[this]{update();});}
void ReferenceImageItem::setSource(const QUrl& value){if(source_==value)return;source_=value;refresh();emit changed();}
void ReferenceImageItem::setBlendMode(QString value){value=value.toLower();if(blend_==value)return;blend_=value;emit changed();}
void ReferenceImageItem::setWarpMode(QString value){value=value.toLower();if(warp_==value)return;warp_=value;emit changed();}
void ReferenceImageItem::setControlPoints(QVariantList value){points_=std::move(value);emit changed();}
void ReferenceImageItem::setImageRect(QRectF value){if(rect_==value)return;rect_=value;emit changed();}
void ReferenceImageItem::setFlipX(bool value){if(flipX_==value)return;flipX_=value;emit changed();}
void ReferenceImageItem::setFlipY(bool value){if(flipY_==value)return;flipY_=value;emit changed();}
void ReferenceImageItem::refresh(){QImageReader reader(source_.toLocalFile());reader.setAutoTransform(true);image_=reader.read();}
ReferenceWarpResult ReferenceImageItem::resolvedWarp()const{QVector<ReferenceControlPoint> controls;for(const auto& value:points_){const auto p=value.toMap();controls.push_back({{p["sourceX"].toDouble(),p["sourceY"].toDouble()},{p["destinationX"].toDouble(),p["destinationY"].toDouble()}});}return solveReferenceWarp(mode(warp_),controls);}
void ReferenceImageItem::paint(QPainter* painter)
{
    if(image_.isNull()||rect_.isEmpty())return;painter->setRenderHint(QPainter::SmoothPixmapTransform);painter->setCompositionMode(composition(blend_));
    const auto warp=resolvedWarp();if(!warp.valid){painter->save();QTransform transform;if(flipX_||flipY_){transform.translate(rect_.center().x(),rect_.center().y());transform.scale(flipX_?-1:1,flipY_?-1:1);transform.translate(-rect_.center().x(),-rect_.center().y());painter->setTransform(transform,true);}painter->drawImage(rect_,image_);painter->restore();return;}
    const QRectF sourceRect(0,0,image_.width(),image_.height());
    for(int row=0;row<16;++row)for(int column=0;column<24;++column){
        const QPointF source[4]={gridPoint(sourceRect,column,row),gridPoint(sourceRect,column+1,row),gridPoint(sourceRect,column+1,row+1),gridPoint(sourceRect,column,row+1)};
        QPointF destination[4];for(int index=0;index<4;++index){QPointF p=source[index];if(flipX_)p.setX(sourceRect.width()-p.x());if(flipY_)p.setY(sourceRect.height()-p.y());destination[index]=warp.map(p);}
        for(const auto triangle: {std::array<int,3>{0,1,2},std::array<int,3>{0,2,3}}){QPolygonF sourceQuad{source[triangle[0]],source[triangle[1]],source[triangle[2]],source[triangle[0]]+source[triangle[2]]-source[triangle[1]]};QPolygonF destinationQuad{destination[triangle[0]],destination[triangle[1]],destination[triangle[2]],destination[triangle[0]]+destination[triangle[2]]-destination[triangle[1]]};QTransform transform;if(!QTransform::quadToQuad(sourceQuad,destinationQuad,transform))continue;QPainterPath clip;clip.addPolygon(QPolygonF{destination[triangle[0]],destination[triangle[1]],destination[triangle[2]]});painter->save();painter->setClipPath(clip);painter->setTransform(transform,true);painter->drawImage(QPointF(0,0),image_);painter->restore();}
    }
}
