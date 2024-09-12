#include "ImageViewer.h"
#include "ui_ImageViewer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>


QImage gamma_correction(const QImage& image, double gamma)
{
    QImage m_image = image.copy();
    for (int y = 0; y < m_image.height(); ++y) {
        for (int x = 0; x < m_image.width(); ++x) {
            QRgb pixel = m_image.pixel(x, y);
            int red = qBound(0, static_cast<int>(qPow(qRed(pixel) / 255.0, gamma) * 255), 255);
            int green = qBound(0, static_cast<int>(qPow(qGreen(pixel) / 255.0, gamma) * 255), 255);
            int blue = qBound(0, static_cast<int>(qPow(qBlue(pixel) / 255.0, gamma) * 255), 255);
            m_image.setPixel(x, y, qRgb(red, green, blue));
        }
    }
    return m_image;
}

ImageViewer::ImageViewer(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImageViewer),
    m_image_size(0, 0),
    m_pan_mode{false}
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->installEventFilter(this);
    connect(ui->buttonOriginalSize, &QToolButton::clicked, this, [this]{ scaleImage(100); });

    ui->labelView->addAction(ui->actionPrintImage);
}

ImageViewer::~ImageViewer()
{
    delete ui;
}

void ImageViewer::resetImage()
{
    ui->labelView->setPixmap(QPixmap(0, 0));
}

void ImageViewer::setImage(const QImage& image)
{
    auto widget_size = ui->scrollArea->viewport()->size();

    m_image = image;
    m_image_size = m_image.size();
    ui->labelView->setMaximumSize(m_image_size.scaled(widget_size, Qt::KeepAspectRatio));

    showImage(m_image, m_default_gamma);

    // Always by default scale the image to fit the viewport.
    ui->buttonFitToWindow->setChecked(true);
}

void ImageViewer::showImage(const QImage& image, const qreal gamma)
{
    QImage new_image = gamma_correction(image, gamma);
    ui->labelView->setPixmap(QPixmap::fromImage(new_image));
}

bool ImageViewer::isQSizeCovered(QSize rect)
{
    auto widget_size = ui->scrollArea->viewport()->size();
    return widget_size.width() >= rect.width() && widget_size.height() >= rect.height();
}

bool ImageViewer::eventFilter(QObject* /*obj*/, QEvent* e) {
    auto e_type = e->type();

    if (e_type == QEvent::Wheel) {
        auto *wheel_event = static_cast<QWheelEvent*>(e);
        auto step = ui->sliderScale->pageStep();
        if (wheel_event->angleDelta().y() < 0) {
            step = -step;
        }
        scaleImage(ui->sliderScale->value() + step);
        e->accept();
        return true;
    }

    if (ui->buttonFitToWindow->isChecked()) {
        if (e_type == QEvent::Resize)
            scaleToFitWindow(true);
    } else if (e_type >= QEvent::MouseButtonPress && e_type <= QEvent::MouseMove) {
        auto *mouse_event = static_cast<QMouseEvent*>(e);
        if (e_type == QEvent::MouseButtonPress && mouse_event->button() == Qt::LeftButton &&
            !isQSizeCovered(ui->labelView->size())) {
            m_mouse_down = mouse_event->globalPos();
            m_pan_mode = true;
            ui->scrollArea->setCursor(Qt::ClosedHandCursor);
        } else if (e_type == QEvent::MouseMove && m_pan_mode) {
            auto dx = mouse_event->globalX() - m_mouse_down.x();
            auto dy = mouse_event->globalY() - m_mouse_down.y();
            m_mouse_down = mouse_event->globalPos();
            if (dx != 0) {
                ui->scrollArea->horizontalScrollBar()->setValue(ui->scrollArea->horizontalScrollBar()->value() - dx);
            }

            if (dy != 0) {
                ui->scrollArea->verticalScrollBar()->setValue(ui->scrollArea->verticalScrollBar()->value() - dy);
            }
        } else if (e_type == QEvent::MouseButtonRelease && mouse_event->button() == Qt::LeftButton) {
            m_pan_mode = false;
            ui->scrollArea->setCursor(Qt::ArrowCursor);
        }
    }
    return false;
}

void ImageViewer::openPrintImageDialog()
{
    QPrinter printer;
    QPrintPreviewDialog dialog(&printer);

    connect(&dialog, &QPrintPreviewDialog::paintRequested, &dialog, [&](QPrinter* previewPrinter) {
        QPainter painter(previewPrinter);
        QRect rect = painter.viewport();
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
        QPixmap pixmap = *ui->labelView->pixmap();
#else
        QPixmap pixmap = ui->labelView->pixmap(Qt::ReturnByValue);
#endif

        QSize size = pixmap.size();
        size.scale(rect.size(), Qt::KeepAspectRatio);
        painter.setViewport(rect.x(), rect.y(), size.width(), size.height());
        painter.setWindow(pixmap.rect());
        painter.drawPixmap(0, 0, pixmap);
    });

    dialog.exec();
}

void ImageViewer::scaleToFitWindow(bool enabled)
{
    // Enable/disable the automatic resizing of the label inside the scrollbar
    ui->scrollArea->setWidgetResizable(enabled);

    // When disabling the fit to window scaling, revert back to the original image size
    if(!enabled) {
        scaleImage(100);
    } else {
        ui->labelView->setMaximumSize(m_image_size.scaled(ui->scrollArea->viewport()->size(), Qt::KeepAspectRatio));
        setSliderValueWithoutSignal(static_cast<int>(ui->labelView->maximumWidth() / static_cast<double>(m_image_size.width()) * 100));
    }
}

void ImageViewer::setNoFitWithoutSignal()
{
    if (ui->buttonFitToWindow->isChecked()) {
        ui->buttonFitToWindow->blockSignals(true);
        ui->scrollArea->setWidgetResizable(false);
        ui->buttonFitToWindow->setChecked(false);
        ui->buttonFitToWindow->blockSignals(false);
    }
}

void ImageViewer::setSliderValueWithoutSignal(int value)
{
    ui->sliderScale->blockSignals(true);
    ui->sliderScale->setValue(value);
    ui->sliderScale->blockSignals(false);
}

void ImageViewer::scaleImage(int scale)
{
    // Clamp scale to the sliderScale min/max
    scale = std::min(ui->sliderScale->maximum(), std::max(ui->sliderScale->minimum(), scale));

    // Make sure the slider is updated when this is called programmatically
    setSliderValueWithoutSignal(scale);

    // Uncheck the fit to window button
    setNoFitWithoutSignal();

    // Update our scale factor
    auto scale_factor = scale / 100.0;

    auto max_size_old = ui->labelView->maximumSize();

    if (m_gamma_instead_of_scaling) {
        // Hijack resizing slider for gamma correction
        qreal gamma = scale_factor / 4.0;
        showImage(m_image, gamma);
    } else {
        // Resize the image (previous default)
        ui->labelView->setMaximumSize(m_image_size * scale_factor);
        ui->labelView->resize(ui->labelView->maximumSize());
    }

    auto factor_change = ui->labelView->maximumWidth() / static_cast<double>(max_size_old.width());

    // Fix scroll bars to zoom into center of viewport instead of the upper left corner
    const auto adjust_scrollbar = [](QScrollBar* scroll_bar, qreal factor) {
        scroll_bar->setValue(static_cast<int>(factor * scroll_bar->value() + ((factor - 1) * scroll_bar->pageStep() / 2)));
    };
    adjust_scrollbar(ui->scrollArea->horizontalScrollBar(), factor_change);
    adjust_scrollbar(ui->scrollArea->verticalScrollBar(), factor_change);
}
