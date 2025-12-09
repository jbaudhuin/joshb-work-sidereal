#ifndef SLIDEWIDGET_H
#define SLIDEWIDGET_H

#include <QWidget>

class QHBoxLayout;
class QDragEnterEvent;
class QDropEvent;

/* =========================== SLIDE WIDGET ========================================= */

class SlideWidget : public QWidget
{
  Q_OBJECT

    public:
        enum Transitions { Transition_NoAnimation,
                           Transition_HorizontalSlide,
                           Transition_Overlay };

        SlideWidget(QWidget *parent = 0);

        void addSlide(QWidget* wdg, int number = -1);
        void setTransitionEffect(Transitions effect)  { this->effect = effect; }
        void setSlide(int number);
        void setSlide(QWidget* wdg);
        QWidget* currentSlide();
        int currentIndex()                            { return index; }
        int count()                                   { return slides.count(); }

    signals:
        void currentSlideChanged();
        void chartDropped(const QString& filePath);

    public slots:
        void nextSlide();

    protected:
        void dragEnterEvent(QDragEnterEvent* event) override;
        void dragMoveEvent(QDragMoveEvent* event) override;
        void dropEvent(QDropEvent* event) override;

    private slots:
        void transitionDone();

    private:
        QList<QWidget*> slides;
        Transitions effect;
        int index;
        QHBoxLayout* layout;
        bool animating;

        void slideSimple     (int i);
        void slideHorizontal (int i);
        void slideOverlay    (int i);
};

#endif // SLIDEWIDGET_H
