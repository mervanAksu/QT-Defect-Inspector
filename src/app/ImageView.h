#pragma once

#include <QImage>
#include <QWidget>

namespace inspector {

/// Bir QImage'ı en-boy oranını koruyarak ölçekleyip ortalanmış çizen widget.
///
/// Neden QLabel::setPixmap değil? QLabel görüntüyü ya orijinal boyutta gösterir ya da
/// oranı bozarak gerer; pencere boyutu değiştikçe yeniden ölçeklemek için ek kod gerekir.
/// paintEvent'i kendimiz yazınca her yeniden çizimde mevcut boyuta göre ölçeklenir.
class ImageView : public QWidget {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clear();
    const QImage& image() const noexcept { return m_image; }

    QSize sizeHint() const override { return {640, 480}; }
    QSize minimumSizeHint() const override { return {320, 240}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
};

}  // namespace inspector
