#ifndef CHART_H
#define CHART_H

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>
#include <Astroprocessor/Gui>

class QVariantAnimation;

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

    // Transient wheel-rotation freeze, engaged only during continuous Play
    // animation so the zodiac ring does not whirl as the (live) ascendant drifts.
    // Captured once on the first locked updateScene() and held for every frame.
    bool  _rotationLocked     = false;
    bool  _haveLockedRotation = false;
    float _lockedRotation     = 0.0f;  // frozen ascendant-derived angle, pre-clockwise
    float _lastRotate         = 0.0f;  // last LIVE (un-frozen) angle, for capture

    // Discrete-step planet slide ("eye candy"): tween the body/marker glyphs
    // from their pre-step positions to the post-step positions over a short
    // duration instead of snapping. Item pointers are stable across a data
    // change (no clearScene), so they key the start/end snapshots. Aspect lines
    // and figures are hidden during the slide and restored on landing.
    QVariantAnimation*                              _slideAnim = nullptr;
    QHash<QGraphicsItem*, QPair<QPointF, qreal>>    _slideStart; // pos, rotation
    int  _slideDurationMs = 0;
    bool _slidePending    = false;
    // Aspect-line crossfade: clones of the pre-step lines that fade out while
    // the live (reused) aspect lines fade in. Keyed start positions of the
    // declination glyphs (by file*K+planetId) so the rebuilt strip can tween.
    QList<QGraphicsLineItem*> _slideAspectGhosts;
    QHash<int, QPointF>       _slideDeclMarkerStart;
    QHash<int, QPointF>       _slideDeclGlyphStart;
    static constexpr int      declSlideKeyMul = 100000;
    int l_zodiacWidth;
    int l_innerRadius;
    int l_cuspideLength;
    bool coloredZodiac;
    bool zodiacDropShadow;
    bool includeAsteroids;
    bool includeCentaurs;
    bool displayDeclination;

    // Aspect Range Navigator animation tuning (lives in the Chart settings tab).
    int  _animDurationMs = 10000; // continuous playback: traverse a range in this
    int  _slideMs        = 600;   // discrete-step planet slide duration (0 = off)

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

    /// Paran focal visualization: a neutral hub at the wheel center with a
    /// spoke to each involved body's marker (radix and/or transit wheel).
    struct ParanFigure {
        QGraphicsEllipseItem*       hub = nullptr;  ///< central node
        QList<QGraphicsLineItem*>   spokes;         ///< hub -> each body marker
        QList<QGraphicsItem*>       spokeMarkers;   ///< marker each spoke tracks
    };
    QList<ParanFigure>               paranFigures;

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
    /// Wheel angle for a body in PV display mode: relocalized into the
    /// reference file's frame for biwheels (per circleStart), else raw pvPos.
    qreal displayPvPos(const A::Star& b, int fileIndex);
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
    void drawParanFigures();
    void clearParanFigures();
    void drawDeclinationAxis();
    void drawDeclinationBodies(int fileIndex);
    void layoutDeclinationGlyphs();
    void clearDeclinationStrip();
    void rebuildDeclinationStrip();

    void snapshotPlanetState(QHash<QGraphicsItem*, QPair<QPointF, qreal>>& into);
    void startPlanetSlide();   // capture end state, reset to start, run the tween
    void finishPlanetSlide();  // snap to end, restore aspect lines/figures
    void clearAspectGhosts();  // remove the crossfade ghost lines
    void abortPlanetSlide();   // hard teardown when the scene is wiped under us

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
    /// Emitted when a wheel time-drag finishes (mouse release after dragging the
    /// zodiac ring to change the chart's moment). Carries the dragged file so
    /// listeners (e.g. the events panel) can react to the deliberate time change.
    void timeDragFinished(AstroFile* draggedFile);
    void planetsSelected(const A::PlanetSet&);

public:
    Chart(QWidget *parent = nullptr);

    void help(QString tag) { requestHelp(tag); }    // called by circle item (because requestHelp() is protected)
    bool isClockwise() { return clockwise; }
    CircleStart startPoint() { return circleStart; }

    // Navigator animation settings (configured in the Chart settings tab).
    int animationDurationMs() const { return _animDurationMs; }
    int planetSlideMs() const { return _slideMs; }

    // Freeze (on==true) / release (on==false) the wheel rotation. Used by the
    // Aspect Range Navigator around continuous Play so the ring stays fixed.
    void lockRotation(bool on);

    // Arm a planet slide for the NEXT moment change (a discrete navigator step).
    // Snapshots the current glyph positions; the slide is kicked off from
    // filesUpdated once the post-step positions are computed. durationMs is the
    // tween length (the caller may shorten it to match a fast click cadence).
    void beginPlanetSlide(int durationMs);

    friend class RotatingCircleItem;
};

#endif // CHART_H
