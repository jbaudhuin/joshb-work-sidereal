#ifndef CHART_H
#define CHART_H

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <Astroprocessor/Gui>

enum CircleStart { Start_ZeroDegree, Start_Ascendent, Start_Outer_Ascendant };

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
        void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;
        bool sceneEventFilter(QGraphicsItem*, QEvent*) override;    // handles events of items
        bool sceneEvent(QEvent *event) override;

    public:
        RotatingCircleItem(QRect rect, const QPen& pen);
        QPainterPath shape() const override;
        QRectF boundingRect() const override { return rect; }

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
    static const int wheelDownBiasPx    = 0;   ///< downward bias for input widgets
    int chartsCount;
    QRectF viewport, viewportBig;
    float zoom;
    QGraphicsView* view;       ///< wheel view (its own scene)
    QGraphicsView* declView;   ///< declination strip view (its own scene)
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
    bool displayDeclination;

    QMap<int, graphicsItemDict> cuspides;
    QMap<int, graphicsItemDict> cuspideLabels;
    QMap<int, graphicsItemDict> planetMarkers;
    QMap<int, graphicsItemDict> planets;
    //QList<QGraphicsSimpleTextItem*> aspectMarkers;
    QList<QGraphicsLineItem*>         aspects;
    QList<QGraphicsItem*>             signIcons;

    /// Midpoint visualization items: chord between B,C and line to A
    struct MidpointFigure {
        QGraphicsLineItem* chordLine = nullptr;    ///< dashed line between B and C
        QGraphicsLineItem* toALine  = nullptr;     ///< orb-weighted line from chord center to A
    };
    QList<MidpointFigure>             midpointFigures;

    /// Declination strip (horizontal axis below the wheel).
    /// X = |declination|; southern bodies above the axis line, northern below.
    static constexpr float declMaxDeg          = 28.0f;
    static constexpr int   declMaxRungs        = 3;
    static constexpr int   declGlyphSpacing    = 16;   ///< px between rungs
    static constexpr int   declGlyphHeightApx  = 18;   ///< approx glyph bbox height
    static constexpr int   declLabelClearance  = 24;   ///< px from baseline to north rung 0
    static constexpr int   declStripAbove      = 4 + declMaxRungs * declGlyphSpacing
                                                + declGlyphHeightApx;
    static constexpr int   declStripBelow      = declLabelClearance
                                                + (declMaxRungs - 1) * declGlyphSpacing
                                                + declGlyphHeightApx + 4;
    static constexpr int   declViewHeight      = declStripAbove + declStripBelow;
    static constexpr int   declStripMargin     = 20;   ///< px L/R inset for axis line
    QList<QGraphicsItem*>          declStripItems;  ///< axis line, ticks, labels
    QMap<int, graphicsItemDict>    declMarkers;     ///< [fileIndex][PlanetId] -> ellipse
    QMap<int, graphicsItemDict>    declGlyphs;      ///< [fileIndex][PlanetId] -> text

    float zodiacWidth() { return l_zodiacWidth * zoom; }
    float innerRadius(int fileIndex = 0);
    int cuspideLength(int fileIndex, int cusp);
    QRect chartRect();
    int   declBaselineY();    ///< Y of the axis in declView scene coords
    float declXForDeg(float absDec);

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
    void drawMidpointFigures();
    void clearMidpointFigures();
    void drawDeclinationAxis();
    void drawDeclinationBodies(int fileIndex);
    void layoutDeclinationGlyphs();
    void clearDeclinationStrip();
    void rebuildDeclinationStrip();

    void fitInView();
    void createScene();
    void updateScene();
    void clearScene();

    void refreshAll();

protected:                            // AstroFileHandler && other implementations
    void filesUpdated(MembersList) override;
    void viewSettingsUpdated(MembersList) override;

    AppSettings defaultSettings() override;
    AppSettings currentSettings() override;
    void applySettings(const AppSettings&) override;
    void setupSettingsEditor(AppSettingsEditor*) override;

    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *ev) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

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
