#ifndef CHART_H
#define CHART_H

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <Astroprocessor/Gui>

enum CircleStart { Start_ZeroDegree = 0, Start_Ascendent = 1 };

class Chart;


class RotatingCircleItem : public QAbstractGraphicsShapeItem
{
    private:
        QRectF rect;
        float dragAngle;
        QDateTime dragDT;
        AstroFile* file;

        float angle(const QPointF& pos);                   // converts coordinate into angle
        Chart* chart();

    protected:
        void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*);
        bool sceneEventFilter(QGraphicsItem*, QEvent*);    // handles events of items
        bool sceneEvent(QEvent *event);

    public:
        RotatingCircleItem(QRect rect, const QPen& pen);
        QPainterPath shape() const;
        QRectF boundingRect() const { return rect; }

        void setFile(AstroFile* f) { file = f; }
        void setHelpTag(QGraphicsItem* item, QString tag);
};


/* =========================== ASTRO MAP SHOW ======================================= */

class Chart : public AstroFileHandler
{
    Q_OBJECT

private:
    typedef QMap<A::PlanetId, QGraphicsItem*> graphicsItemDict;

    static const int defaultChartRadius = 250;
    int chartsCount;
    QRectF viewport, viewportBig;
    float zoom;
    QGraphicsView* view;
    RotatingCircleItem* circle;
    //A::AspectList synAspects;

    CircleStart circleStart;
    bool clockwise;
    int l_zodiacWidth;
    int l_innerRadius;
    int l_cuspideLength;
    bool coloredZodiac;
    bool zodiacDropShadow;
    bool includeAsteroids;
    bool includeCentaurs;

    QMap<int, graphicsItemDict> cuspides;
    QMap<int, graphicsItemDict> cuspideLabels;
    QMap<int, graphicsItemDict> planetMarkers;
    QMap<int, graphicsItemDict> planets;
    //QList<QGraphicsSimpleTextItem*> aspectMarkers;
    QList<QGraphicsLineItem*>         aspects;
    QList<QGraphicsItem*>             signIcons;

    float zodiacWidth() { return l_zodiacWidth * zoom; }
    float innerRadius(int fileIndex = 0);
    int cuspideLength(int fileIndex, int cusp);
    QRect chartRect();

    int normalPlanetPosX(QGraphicsItem* planet, QGraphicsItem* marker);
    const QPen& aspectPen(const A::Aspect& asp);
    const QPen& planetMarkerPen(const A::Planet& p, int fileIndex);
    QColor planetColor(const A::Planet& p, int fileIndex);
    QColor planetShapeColor(const A::Planet& p, int fileIndex);
    QGraphicsItem* getCircleMarker(const A::Planet* p);

    void drawPlanets(int fileIndex);
    void drawStars(int fileIndex);
    void drawCuspides(int fileIndex);
    void updatePlanetsAndCusps(int fileIndex);
    void updateAspects();

    void fitInView();
    void createScene();
    void updateScene();
    void clearScene();

    void refreshAll();

protected:                            // AstroFileHandler && other implementations
    void filesUpdated(MembersList);

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void applySettings(const AppSettings&);
    void setupSettingsEditor(AppSettingsEditor*);

    bool eventFilter(QObject *, QEvent *);
    void resizeEvent(QResizeEvent *ev);

public slots:
    void onPlanetsSelected(const A::PlanetSet&) { }

signals:
    void planetSelected(A::PlanetId, int fileIndex);
    void planetsSelected(const A::PlanetSet&);

public:
    Chart(QWidget *parent = nullptr);

    void help(QString tag) { requestHelp(tag); }    // called by circle item (because requestHelp() is protected)
    bool isClockwise() { return clockwise; }
    CircleStart startPoint() { return circleStart; }

    friend class RotatingCircleItem;
};

#endif // CHART_H
