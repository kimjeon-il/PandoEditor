#pragma once
#include <QAbstractListModel>
#include <QVariantMap>
#include <QVariantList>
#include <algorithm>

// Stable delegates during camera motion, including labels crossing the viewport
// edge. Only inserted/removed labels create/destroy their text and flag nodes.
class LabelPlacementModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { Content=Qt::UserRole, LabelX, LabelY };
    explicit LabelPlacementModel(QObject* parent=nullptr):QAbstractListModel(parent){}
    int rowCount(const QModelIndex& parent={}) const override {return parent.isValid()?0:int(rows_.size());}
    QVariant data(const QModelIndex& index,int role) const override {
        if(!index.isValid()||index.row()<0||index.row()>=rows_.size())return {};
        if(role==Content)return rows_.at(index.row());
        const auto row=rows_.at(index.row()).toMap();
        return role==LabelX?row.value("x"):role==LabelY?row.value("y"):QVariant{};
    }
    QHash<int,QByteArray> roleNames() const override {return {{Content,"modelData"},{LabelX,"labelX"},{LabelY,"labelY"}};}
    void setRows(const QVariantList& rows) {
        const auto identity=[](const QVariant& row){return row.toMap().value("ref");};
        // Camera reprojection normally changes only coordinates. Comparing the
        // complete refs once avoids the quadratic membership scan while keeping
        // exactly the same identity contract (including non-map/duplicate refs).
        const bool sameOrder=rows.size()==rows_.size()&&
            std::equal(rows.cbegin(),rows.cend(),rows_.cbegin(),
                [&](const auto& a,const auto& b){return identity(a)==identity(b);});
        if(!sameOrder) {
            for(qsizetype i=rows_.size();i>0;--i) {
                const auto key=identity(rows_[i-1]);
                if(std::none_of(rows.cbegin(),rows.cend(),[&](const auto& row){return identity(row)==key;})) {
                    beginRemoveRows({},int(i-1),int(i-1));rows_.removeAt(i-1);endRemoveRows();
                }
            }
            for(qsizetype i=0;i<rows.size();++i) {
                const auto key=identity(rows[i]);
                qsizetype existing=i;
                while(existing<rows_.size()&&identity(rows_[existing])!=key)++existing;
                if(existing==rows_.size()) {
                    beginInsertRows({},int(i),int(i));rows_.insert(i,rows[i]);endInsertRows();
                } else if(existing!=i) {
                    beginMoveRows({},int(existing),int(existing),{},int(i));rows_.move(existing,i);endMoveRows();
                }
            }
        }
        for(qsizetype i=0;i<rows.size();++i)if(rows[i]!=rows_[i]) {
            auto before=rows_[i].toMap(),after=rows[i].toMap();
            QList<int> roles;
            if(before.take("x")!=after.take("x"))roles.append(LabelX);
            if(before.take("y")!=after.take("y"))roles.append(LabelY);
            // Never re-evaluate SVG data URLs/text when only the camera moved.
            if(before!=after)roles.append(Content);
            rows_[i]=rows[i];emit dataChanged(index(int(i)),index(int(i)),roles);
        }
    }
private:
    QVariantList rows_;
};
