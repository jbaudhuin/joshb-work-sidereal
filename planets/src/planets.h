#ifndef PLANETS_H
#define PLANETS_H

#include <QQuickView>
#include <Astroprocessor/Gui>
#include <Astroprocessor/Output>

class QRadioButton;
class QDeclarativeItem;


/* =========================== ASTRO QML VIEW ======================================= */

class AstroQmlView : public QQuickView
{
    Q_OBJECT

    private:
        void fitSceneInView();

    protected:
        void resizeEvent(QResizeEvent*) override { fitSceneInView(); }

    public slots:

    signals:                        // bindings to QML functions
        void objectClicked (QString name);
        void requestHelp(QString tag);

    public:
		AstroQmlView(QWindow * parent = NULL);
		void setSource(const QUrl& url);

};


/* =========================== ASTRO PLANETS SHOW =================================== */

class Planets : public AstroFileHandler
{
    Q_OBJECT

    private:
        A::PlanetsOrder order;

        AstroQmlView* view;
        QList<QQuickItem*> cardItems, labelItems;

        QRadioButton* sortByPlanets;
        QRadioButton* sortByHouses;
        QRadioButton* sortByPower;
        QRadioButton* sortByElement;

        void drawPlanets();

    private slots:
        void orderChanged();
        void objectClicked(QString name);

    signals:
        void planetSelected(A::PlanetId, int fileIndex);

    protected:                            // AstroFileHandler && other implementations
        void filesUpdated(MembersList) override;
        void dragEnterEvent(QDragEnterEvent* event) override;
        void dragMoveEvent(QDragMoveEvent* event) override;
        void dropEvent(QDropEvent* event) override;

    public:
        Planets(QWidget *parent = 0);

};

#endif // PLANETS_H
