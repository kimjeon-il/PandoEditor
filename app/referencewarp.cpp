#include "referencewarp.h"

#include <algorithm>
#include <cmath>
#include <QLineF>

namespace {
using Matrix = QVector<QVector<double>>;

bool solve(Matrix matrix, QVector<double> &result)
{
    const int n = matrix.size();
    for (int column = 0; column < n; ++column) {
        int pivot = column;
        for (int row = column + 1; row < n; ++row)
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column]))
                pivot = row;
        if (std::abs(matrix[pivot][column]) < 1e-12)
            return false;
        if (pivot != column)
            matrix.swapItemsAt(pivot, column);
        const double divisor = matrix[column][column];
        for (int item = column; item <= n; ++item)
            matrix[column][item] /= divisor;
        for (int row = 0; row < n; ++row) {
            if (row == column)
                continue;
            const double factor = matrix[row][column];
            for (int item = column; item <= n; ++item)
                matrix[row][item] -= factor * matrix[column][item];
        }
    }
    result.resize(n);
    for (int row = 0; row < n; ++row)
        result[row] = matrix[row][n];
    return true;
}

double radial(double distanceSquared)
{
    return distanceSquared > 1e-20 ? distanceSquared * std::log(distanceSquared) : 0.0;
}

bool distinctSources(const QVector<ReferenceControlPoint> &points)
{
    for (int a = 0; a < points.size(); ++a)
        for (int b = a + 1; b < points.size(); ++b)
            if (QLineF(points[a].source, points[b].source).length() < 1e-9)
                return false;
    return true;
}
}

QPointF ReferenceWarpResult::map(const QPointF &point) const
{
    if (!valid)
        return point;
    if (resolvedMode == ReferenceWarpMode::Similarity) {
        const double x = point.x(), y = point.y();
        return {xCoefficients[0] * x - xCoefficients[1] * y + xCoefficients[2],
                xCoefficients[1] * x + xCoefficients[0] * y + xCoefficients[3]};
    }
    if (resolvedMode == ReferenceWarpMode::Affine)
        return {xCoefficients[0] * point.x() + xCoefficients[1] * point.y() + xCoefficients[2],
                yCoefficients[0] * point.x() + yCoefficients[1] * point.y() + yCoefficients[2]};
    if (resolvedMode == ReferenceWarpMode::Projective) {
        const double denominator = xCoefficients[6] * point.x() + xCoefficients[7] * point.y() + 1.0;
        if (std::abs(denominator) < 1e-12)
            return point;
        return {(xCoefficients[0] * point.x() + xCoefficients[1] * point.y() + xCoefficients[2]) / denominator,
                (xCoefficients[3] * point.x() + xCoefficients[4] * point.y() + xCoefficients[5]) / denominator};
    }
    const int count = controlPoints.size();
    double x = xCoefficients[count] + xCoefficients[count + 1] * point.x() + xCoefficients[count + 2] * point.y();
    double y = yCoefficients[count] + yCoefficients[count + 1] * point.x() + yCoefficients[count + 2] * point.y();
    for (int index = 0; index < count; ++index) {
        const QPointF delta = point - controlPoints[index].source;
        const double basis = radial(delta.x() * delta.x() + delta.y() * delta.y());
        x += xCoefficients[index] * basis;
        y += yCoefficients[index] * basis;
    }
    return {x, y};
}

ReferenceWarpResult solveReferenceWarp(ReferenceWarpMode requested,
                                       const QVector<ReferenceControlPoint> &points)
{
    ReferenceWarpResult result;
    result.controlPoints = points;
    if (!distinctSources(points)) {
        result.error = QStringLiteral("기준점이 중복됩니다.");
        return result;
    }
    ReferenceWarpMode mode = requested;
    if (mode == ReferenceWarpMode::Auto) {
        if (points.size() >= 4) mode = ReferenceWarpMode::Projective;
        else if (points.size() >= 3) mode = ReferenceWarpMode::Affine;
        else mode = ReferenceWarpMode::Similarity;
    }
    const int required = mode == ReferenceWarpMode::Similarity ? 2
                       : mode == ReferenceWarpMode::Affine ? 3
                       : mode == ReferenceWarpMode::Projective ? 4 : 3;
    if (points.size() < required) {
        result.error = QStringLiteral("기준점이 부족합니다.");
        return result;
    }
    result.resolvedMode = mode;
    if (mode == ReferenceWarpMode::Similarity) {
        Matrix matrix;
        for (const auto &point : points) {
            matrix.push_back({point.source.x(), -point.source.y(), 1, 0, point.destination.x()});
            matrix.push_back({point.source.y(), point.source.x(), 0, 1, point.destination.y()});
        }
        // Least squares for more than two control points.
        Matrix normal(4, QVector<double>(5));
        for (const auto &row : matrix)
            for (int a = 0; a < 4; ++a) {
                for (int b = 0; b < 4; ++b) normal[a][b] += row[a] * row[b];
                normal[a][4] += row[a] * row[4];
            }
        if (!solve(normal, result.xCoefficients)) result.error = QStringLiteral("기준점 배치가 불안정합니다.");
    } else if (mode == ReferenceWarpMode::Affine) {
        Matrix normal(3, QVector<double>(4)), normalY(3, QVector<double>(4));
        for (const auto &point : points) {
            const double values[3] = {point.source.x(), point.source.y(), 1.0};
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) normal[a][b] += values[a] * values[b];
                normal[a][3] += values[a] * point.destination.x();
                normalY[a][0] = normal[a][0]; normalY[a][1] = normal[a][1]; normalY[a][2] = normal[a][2];
                normalY[a][3] += values[a] * point.destination.y();
            }
        }
        if (!solve(normal, result.xCoefficients) || !solve(normalY, result.yCoefficients))
            result.error = QStringLiteral("기준점 배치가 불안정합니다.");
    } else if (mode == ReferenceWarpMode::Projective) {
        Matrix normal(8, QVector<double>(9));
        for (const auto &point : points) {
            const double x=point.source.x(), y=point.source.y(), u=point.destination.x(), v=point.destination.y();
            const QVector<QVector<double>> rows{{x,y,1,0,0,0,-u*x,-u*y,u},{0,0,0,x,y,1,-v*x,-v*y,v}};
            for (const auto &row : rows) for (int a=0;a<8;++a) {
                for (int b=0;b<8;++b) normal[a][b]+=row[a]*row[b];
                normal[a][8]+=row[a]*row[8];
            }
        }
        if (!solve(normal, result.xCoefficients)) result.error = QStringLiteral("기준점 배치가 불안정합니다.");
    } else {
        const int count = points.size(), size = count + 3;
        Matrix xMatrix(size, QVector<double>(size + 1)), yMatrix(size, QVector<double>(size + 1));
        for (int row = 0; row < count; ++row) {
            for (int column = 0; column < count; ++column) {
                const QPointF delta = points[row].source - points[column].source;
                xMatrix[row][column] = yMatrix[row][column] = radial(delta.x()*delta.x()+delta.y()*delta.y());
            }
            xMatrix[row][count]=yMatrix[row][count]=1;
            xMatrix[row][count+1]=yMatrix[row][count+1]=points[row].source.x();
            xMatrix[row][count+2]=yMatrix[row][count+2]=points[row].source.y();
            xMatrix[row][size]=points[row].destination.x();
            yMatrix[row][size]=points[row].destination.y();
            xMatrix[count][row]=yMatrix[count][row]=1;
            xMatrix[count+1][row]=yMatrix[count+1][row]=points[row].source.x();
            xMatrix[count+2][row]=yMatrix[count+2][row]=points[row].source.y();
        }
        if (!solve(xMatrix,result.xCoefficients)||!solve(yMatrix,result.yCoefficients))
            result.error=QStringLiteral("기준점 배치가 불안정합니다.");
    }
    if (!result.error.isEmpty()) return result;
    result.valid = true;
    double squared = 0;
    for (const auto &point : points) {
        const double error = QLineF(result.map(point.source), point.destination).length();
        squared += error * error;
        result.maximumError = std::max(result.maximumError, error);
    }
    result.rmsError = std::sqrt(squared / points.size());
    return result;
}
