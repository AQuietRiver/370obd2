#include "gui/LiveDataChart.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace gui {

LiveDataChart::LiveDataChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(210);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void LiveDataChart::addSample(
    const QString& sensor, double value, const QString& unit, qint64 timestampMs) {
    Series& series = series_[sensor];
    series.unit = unit;
    series.samples.push_back({timestampMs, value});
    const qint64 cutoff = timestampMs - historyWindowMs_;
    while (!series.samples.isEmpty() && series.samples.front().timestampMs < cutoff) {
        series.samples.removeFirst();
    }
    if (selectedSensor_.isEmpty()) {
        selectedSensor_ = sensor;
    }
    update();
}

void LiveDataChart::selectSeries(const QString& sensor) {
    if (series_.contains(sensor)) {
        selectedSensor_ = sensor;
        update();
    }
}

void LiveDataChart::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(32, 36, 37));

    const QRectF plot = QRectF(rect()).adjusted(64, 35, -20, -36);
    const QColor grid(62, 69, 70);
    const QColor text(158, 166, 163);
    painter.setPen(text);
    painter.drawText(QRectF(14, 8, width() - 28, 20), Qt::AlignLeft,
        selectedSensor_.isEmpty() ? "Select a sensor to graph" : selectedSensor_);
    painter.setPen(grid);
    painter.drawRect(plot);

    const auto found = series_.constFind(selectedSensor_);
    if (found == series_.cend() || found->samples.isEmpty()) {
        painter.setPen(text);
        painter.drawText(plot, Qt::AlignCenter, "No numeric samples yet");
        return;
    }

    const Series& series = found.value();
    double minimum = series.samples.front().value;
    double maximum = minimum;
    for (const Sample& sample : series.samples) {
        minimum = std::min(minimum, sample.value);
        maximum = std::max(maximum, sample.value);
    }
    const double rawRange = maximum - minimum;
    const double padding = rawRange > 0.0 ? rawRange * 0.1 : std::max(std::abs(maximum) * 0.05, 1.0);
    minimum -= padding;
    maximum += padding;

    const qint64 latest = series.samples.back().timestampMs;
    const qint64 earliest = latest - historyWindowMs_;
    painter.setPen(grid);
    for (int index = 1; index < 4; ++index) {
        const qreal y = plot.top() + plot.height() * index / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    painter.setPen(text);
    painter.drawText(QRectF(3, plot.top() - 7, 55, 16), Qt::AlignRight,
        QString::number(maximum, 'f', 1));
    painter.drawText(QRectF(3, plot.bottom() - 8, 55, 16), Qt::AlignRight,
        QString::number(minimum, 'f', 1));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 8, 60, 18), Qt::AlignLeft, "-60 s");
    painter.drawText(QRectF(plot.right() - 60, plot.bottom() + 8, 60, 18), Qt::AlignRight, "now");
    painter.drawText(QRectF(plot.right() - 130, 8, 130, 20), Qt::AlignRight,
        QString("%1 samples · %2").arg(series.samples.size()).arg(series.unit));

    QPainterPath path;
    for (qsizetype index = 0; index < series.samples.size(); ++index) {
        const Sample& sample = series.samples[index];
        const qreal x = plot.left() + plot.width()
            * static_cast<qreal>(sample.timestampMs - earliest) / historyWindowMs_;
        const qreal y = plot.bottom() - plot.height() * (sample.value - minimum) / (maximum - minimum);
        if (index == 0) path.moveTo(x, y); else path.lineTo(x, y);
    }
    painter.setPen(QPen(QColor(208, 138, 60), 2));
    painter.drawPath(path);
}

} // namespace gui
