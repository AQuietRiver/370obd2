#pragma once

#include <QHash>
#include <QVector>
#include <QWidget>

namespace gui {

class LiveDataChart : public QWidget {
    Q_OBJECT

public:
    explicit LiveDataChart(QWidget* parent = nullptr);

    void addSample(const QString& sensor, double value, const QString& unit, qint64 timestampMs);
    void selectSeries(const QString& sensor);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Sample { qint64 timestampMs; double value; };
    struct Series { QString unit; QVector<Sample> samples; };

    QHash<QString, Series> series_;
    QString selectedSensor_;
    qint64 historyWindowMs_ = 60000;
};

} // namespace gui
