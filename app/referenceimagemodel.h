#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>

// Image ids own delegates. Transform previews and history only change their
// data, so an active pointer handler survives until the real release event.
class ReferenceImageModel final : public QAbstractListModel
{
public:
    enum Role { Content = Qt::UserRole };
    explicit ReferenceImageModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : int(rows_.size()); }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= rows_.size()) return {};
        return role == Content ? rows_.at(index.row()) : QVariant{};
    }

    QHash<int, QByteArray> roleNames() const override { return {{Content, "modelData"}}; }

    void setRows(const QVariantList &rows)
    {
        const auto identity = [](const QVariant &row) { return row.toMap().value("id"); };
        const bool sameOrder = rows.size() == rows_.size() &&
            std::equal(rows.cbegin(), rows.cend(), rows_.cbegin(),
                [&](const auto &a, const auto &b) { return identity(a) == identity(b); });
        if (!sameOrder) {
            for (qsizetype i = rows_.size(); i > 0; --i) {
                const auto id = identity(rows_[i - 1]);
                if (std::none_of(rows.cbegin(), rows.cend(), [&](const auto &row) { return identity(row) == id; })) {
                    beginRemoveRows({}, int(i - 1), int(i - 1));
                    rows_.removeAt(i - 1);
                    endRemoveRows();
                }
            }
            for (qsizetype i = 0; i < rows.size(); ++i) {
                const auto id = identity(rows[i]);
                qsizetype existing = i;
                while (existing < rows_.size() && identity(rows_[existing]) != id) ++existing;
                if (existing == rows_.size()) {
                    beginInsertRows({}, int(i), int(i));
                    rows_.insert(i, rows[i]);
                    endInsertRows();
                } else if (existing != i) {
                    beginMoveRows({}, int(existing), int(existing), {}, int(i));
                    rows_.move(existing, i);
                    endMoveRows();
                }
            }
            // Normally ids are unique UUIDs. Keep loaded records synchronized
            // even if an older index contains repeated ids.
            if (rows_.size() > rows.size()) {
                beginRemoveRows({}, int(rows.size()), int(rows_.size() - 1));
                rows_.resize(rows.size());
                endRemoveRows();
            }
        }
        for (qsizetype i = 0; i < rows.size(); ++i) {
            if (rows_[i] == rows[i]) continue;
            rows_[i] = rows[i];
            emit dataChanged(index(int(i)), index(int(i)), {Content});
        }
    }

private:
    QVariantList rows_;
};
