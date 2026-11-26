#include "ImageView.h"

#include <QPainter>

namespace inspector {

ImageView::ImageView(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);  // tüm alanı kendimiz boyuyoruz
}

void ImageView::setImage(const QImage& image)
{
    m_image = image;  // QImage implicitly shared: piksel kopyası yok
    update();         // hemen çizmez; olay döngüsüne "yeniden çiz" isteği bırakır
}

void ImageView::clear()
{
    m_image = QImage();
    update();
}

void ImageView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 30, 30));

    if (m_image.isNull()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, tr("No image"));
        return;
    }

    // Oranı koruyarak sığdır ve ortala.
    QSize target = m_image.size();
    target.scale(size(), Qt::KeepAspectRatio);
    const QRect area(QPoint((width() - target.width()) / 2, (height() - target.height()) / 2),
                     target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(area, m_image);
}

}  // namespace inspector
