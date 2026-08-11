#ifndef ASTRO_GUI_H
#define ASTRO_GUI_H

#include <QStringList>
#include <QVariant>
#include <QMetaType>
#include <QTimeZone>

#include <deque>

#include "astro-data.h"
#include "astro-calc.h"
#include "appsettings.h"
#include "../zodiac/src/afileinfo.h"

class QStandardItemModel;
class QAbstractItemModel;

using A::ADateRange;

Q_DECLARE_METATYPE(ADateRange)

/* ========================= DISPLAY SETTINGS ============================== */

/// Singleton that owns the global view/toolbar state: house system, zodiac,
/// aspect set, and aspect mode.  These settings apply to all open charts
/// simultaneously and are NOT persisted inside chart files.
///
/// Harmonic is intentionally excluded — it is genuine per-file state.

class DisplaySettings : public QObject {
    Q_OBJECT

  public:
    /// Access the singleton.  Created on first call.
    static DisplaySettings& instance();

    // --- getters ---
    A::HouseSystemId houseSystem() const { return _houseSystem; }
    A::ZodiacId      zodiac()      const { return _zodiac; }
    A::AspectSetId   aspectSet()   const { return _aspectSet; }
    A::aspectModeEnum aspectMode() const { return A::aspectMode; }
    A::aspectModeEnum primaryFrame() const;
    bool              useGreatCircle() const;
    bool              usePrimeVerticalDisplay() const;

    // --- setters (emit changed() when value differs) ---
    void setHouseSystem(A::HouseSystemId);
    void setZodiac(A::ZodiacId);
    void setAspectSet(A::AspectSetId, bool force = false);
    void setAspectMode(A::aspectModeEnum);  // legacy: maps to (frame, gc)
    void setPrimaryFrame(A::aspectModeEnum);  // amcEcliptic or amcEquatorial
    void setUseGreatCircle(bool);
    void setUsePrimeVerticalDisplay(bool);

    /// Batch-update: set all four at once, emit a single changed() signal
    /// with the union of flags.  Used by toolbar / setupFile.
    void apply(A::HouseSystemId hsys,
               A::ZodiacId     zod,
               A::AspectSetId  aset,
               A::aspectModeEnum mode,
               bool forceAspect = false);

    /// Batch-update with the new decoupled aspect-mode triple.
    void apply(A::HouseSystemId hsys,
               A::ZodiacId      zod,
               A::AspectSetId   aset,
               A::aspectModeEnum frame,
               bool              gc,
               bool              pvDisplay,
               bool              forceAspect = false);

  signals:
    /// Emitted after one or more settings changed.
    /// The Members mask uses AstroFile::HouseSystem / Zodiac /
    /// AspectSet / AspectMode flags so handlers can test efficiently.
    void changed(int flags);   // int instead of Members to avoid
                               // forward-decl issues

  private:
    DisplaySettings();
    Q_DISABLE_COPY(DisplaySettings)

    A::HouseSystemId  _houseSystem;
    A::ZodiacId       _zodiac;
    A::AspectSetId    _aspectSet;
    // AspectMode lives in the existing global A::aspectMode
};

/* =========================== ASTRO FILE ================================== */

class AstroFile : public QObject, public A::EventStore {
    Q_OBJECT;

  public:
    enum Member {
        None         = 0x0,

        // --- File data (persisted per chart) ---
        Name         = 0x1,
        Type         = 0x2,
        GMT          = 0x4,
        Timezone     = 0x8,
        Location     = 0x10,
        LocationName = 0x20,
        Comment      = 0x40,

        // --- View settings (toolbar/UI state, not saved with file) ---
        HouseSystem  = 0x80,
        Zodiac       = 0x100,
        AspectSet    = 0x200,
        AspectMode   = 0x400,
        Harmonic     = 0x800,

        // --- Meta ---
        ChangedState = 0x1000,

        // PV (prime-vertical) display toggle: changes chart wheel + aspect
        // lines but does not invalidate events.
        ChartDisplayMode = 0x2000,

        // --- Group masks ---
        FileData     = Name | Type | GMT | Timezone | Location
                       | LocationName | Comment,
        ViewSettings = HouseSystem | Zodiac | AspectSet | AspectMode
                       | Harmonic | ChartDisplayMode,
        All          = 0x3FFF
    };

    Q_DECLARE_FLAGS(Members, Member)

    AstroFile(QObject* parent = nullptr);
    virtual ~AstroFile() { }

    QString            fileName() const;
    static QString     typeToString(unsigned type);
    static FileType    typeFromString(const QString& str);
    AstroFile::Members diff(AstroFile* other) const;

    void save();
    void saveAs();
    void load(const AFileInfo& name);

    /// Turn this file into a midpoint composite of the two source charts.
    /// The file's own GMT/location remain the houses reference; planet
    /// positions are synthesized in recalculate().
    void setCompositeSources(const AFileInfo& a, const AFileInfo& b);
    bool hasCompositeSources() const { return _hasCompositeSources; }
    const AFileInfo& compositeFile(int i) const { return _compositeFiles[i]; }
    const A::InputData& compositeInput(int i) const { return _compositeInputs[i]; }

    /// Read the calculation inputs (GMT, tz, location, calendar, time mode)
    /// from a chart file without constructing an AstroFile.
    static A::InputData loadInputData(const AFileInfo& fi);

    void suspendUpdate() { _holdUpdate = true; }
    bool isSuspendedUpdate() const { return _holdUpdate; }
    void resumeUpdate();

    void clearUnsavedState();
    bool hasUnsavedChanges() const { return _unsavedChanges; }
    bool isEmpty() const { return scope.planets.count() == 0; }

    void setName(const QString& name);
    void setFileInfo(const AFileInfo& fi) { _fileInfo = fi; }
    void setType(const FileType type);
    void setGMT(const QDateTime& gmt);
    void setTimezone(double zone);
    void setCalendarType(A::CalendarType ct);
    void setTimeMode(A::TimeMode tm);
    void setLocation(const QVector3D location);
    void setLocationName(const QString& location);
    void setComment(const QString& comment);
    void setHouseSystem(A::HouseSystemId system);
    void setZodiac(A::ZodiacId zod);
    void setAspectSet(A::AspectSetId set, bool force = false);
    void setAspectMode(const A::aspectModeType& mode);

    /// Copy current DisplaySettings into InputData and recalculate if needed.
    /// Returns the Members mask of what actually changed.
    /// Does NOT call change() — the caller decides how to notify.
    Members stampDisplaySettings();
    void setHarmonic(double harmonic);
    void setBaseChart(const QDateTime& baseGmt);
    void clearBaseChart();
    void setTimezoneLocked(bool locked);

    void setFocalPlanets(const A::PlanetSet& fp = {}) { _focalPlanets = fp; }
    void setFocalPlanets(const A::PlanetSet& fp, bool notify);
    
    const A::EventTypeSet& getTransitEventOptions() const { return _transitEventOptions; }
    void setTransitEventOptions(const A::EventTypeSet& opts);

    // Per-chart, display-only selection of which heliacal apparition phases
    // (MF/Acr/Cul/Cs/EL) surface as their own rows in the event list. Kept out
    // of the EventType set so toggling never triggers a finder recompute.
    enum HeliacalPhaseBit {
        // Fixed stars & outer planets (5-stop culmination model)
        hpMF = 1, hpAcr = 2, hpCul = 4, hpCs = 8, hpEL = 16,
        // Inner planets (elongation model): greatest elongation (G*e),
        // first visibility (*F: EF/MF), last visibility (*L: EL/ML)
        hpElong = 32, hpFirst = 64, hpLast = 128
    };
    static constexpr unsigned kHeliacalPhaseDefault =
        hpAcr | hpCul | hpCs | hpElong;
    unsigned getHeliacalPhaseMask() const { return _heliacalPhaseMask; }
    void setHeliacalPhaseMask(unsigned m);

    A::EventOptions::skipper getTransitSkipByDuration() const { return _transitSkipByDuration; }
    void setTransitSkipByDuration(A::EventOptions::skipper s);

    bool getTransitAutoReconcile() const { return _transitAutoReconcile; }
    void setTransitAutoReconcile(bool on);

    A::EventType getOriginEventType() const { return _originEventType; }
    void setOriginEventType(A::EventType et) { _originEventType = et; }

    // For paran charts: the bodies that form the paran event. fileId 0 =
    // transit, 1 = natal ex-precessed. A midpoint participant (e.g.
    // Ven-r/Mar-r) carries both planetId() and planetId2() via isMidpt().
    using ParanGroupEntry = A::ChartPlanetId;
    const QVector<ParanGroupEntry>& getParanGroupPlanets() const { return _paranGroupPlanets; }
    void setParanGroupPlanets(const QVector<ParanGroupEntry>& g) { _paranGroupPlanets = g; }

    // Paran cycling: per-day in-orb moments of the focal cluster + that day's
    // orb°, copied from the clicked paran event. Transient (not persisted) —
    // regenerated whenever a paran event is clicked.
    const QVector<QPair<QDateTime, qreal>>& getParanOccurrences() const
    {
        return _paranOccurrences;
    }
    void setParanOccurrences(const QVector<QPair<QDateTime, qreal>>& o)
    {
        _paranOccurrences = o;
    }

    // Optional per-occurrence phase labels (apparition stops: MF/Acr/Cul/Cs/EL),
    // 1:1 with getParanOccurrences(). Empty for parans. Transient like the above.
    const QStringList& paranOccurrenceLabels() const
    {
        return _paranOccurrenceLabels;
    }
    void setParanOccurrenceLabels(const QStringList& l)
    {
        _paranOccurrenceLabels = l;
    }

    // Aspect Range Navigator: in-orb range + exact moment of a ranged event.
    const A::ADateTimeRange& getAspectRange() const { return _aspectRange; }
    void setAspectRange(const A::ADateTimeRange& r) { _aspectRange = r; }
    const QDateTime& getAspectExact() const { return _aspectExact; }
    void             setAspectExact(const QDateTime& t) { _aspectExact = t; }

    bool getDrawFocalExpand() const { return _drawFocalExpand; }
    void setDrawFocalExpand(bool b) { _drawFocalExpand = b; }
    A::AspectSetId getDrawOverrideAspectSet() const { return _drawOverrideAspectSet; }
    void setDrawOverrideAspectSet(A::AspectSetId s) { _drawOverrideAspectSet = s; }

    const QMap<A::EventType, unsigned>& getTransitHarmonicRestrictions() const { return _transitHarmonicRestrictions; }
    void setTransitHarmonicRestrictions(const QMap<A::EventType, unsigned>& r) { _transitHarmonicRestrictions = r; }

    QString          getName() const { return _fileInfo.baseName(); }
    QString          getBaseName() const;
    QString          getDisplayName() const;
    bool             isTimezoneLocked() const { return _timezoneLocked; }
    const AFileInfo& fileInfo() const { return _fileInfo; }

    const QString&   getComment() const { return comment; }
    FileType         getType() const { return type; }
    const QVector3D& getLocation() const { return scope.inputData.location(); }
    const QString&   getLocationName() const { return locationName; }
    const QDateTime& getGMT() const { return scope.inputData.GMT(); }
    double           getTimezone() const { return scope.inputData.tz(); }

    A::Horoscope&       horoscope() { return scope; }
    const A::Horoscope& horoscope() const { return scope; }

    A::HouseSystemId    getHouseSystem() const
    {
        return scope.inputData.houseSystem();
    }

    A::ZodiacId    getZodiac() const { return scope.inputData.zodiac(); }

    A::AspectSetId getAspectSetId() const
    {
        return scope.inputData.aspectSet();
    }

    const A::AspectsSet& getAspectSet() const
    {
        return A::getAspectSet(getAspectSetId());
    }

    A::aspectModeEnum       getAspectMode() const { return A::aspectMode; }
    double                  getHarmonic() const { return scope.harmonic; }
    bool                    hasBaseChart() const
    {
        return scope.inputData.hasBaseChart();
    }
    const QDateTime& getBaseChartGMT() const
    {
        return scope.inputData.baseGMT();
    }

    QDateTime getLocalTime() const
    {
        // For LMT/LAT, getTimezone() is only an informational snapshot of
        // the effective offset, not something that can exactly reproduce
        // the local time (LAT's true UTC difference includes the
        // continuously-varying Equation of Time, so it essentially never
        // lands on a whole second). Recompute directly from longitude
        // instead of replaying that stored value.
        return A::UTCtoLocal(getGMT(), getTimezone(), getLocation().x(),
                              getTimeMode(), getCalendarType());
    }

    A::CalendarType getCalendarType() const
    {
        return scope.inputData.calendarType();
    }
    A::TimeMode getTimeMode() const { return scope.inputData.timeMode(); }

    // Transit date range (per-tab state for transits view)
    QDate getTransitStartDate() const { return _transitStartDate; }
    void setTransitStartDate(const QDate& date) { _transitStartDate = date; }
    QString getTransitDuration() const { return _transitDuration; }
    void setTransitDuration(const QString& duration) { _transitDuration = duration; }

    // Transit location (per-tab state — persists across file-2 open/close)
    const QVector3D& getTransitLocation() const { return _transitLocation; }
    void  setTransitLocation(const QVector3D& loc) { _transitLocation = loc; }
    const QString& getTransitLocationName() const { return _transitLocationName; }
    void  setTransitLocationName(const QString& name) { _transitLocationName = name; }
    short getTransitTimezone() const { return _transitTimezone; }
    void  setTransitTimezone(short tz) { _transitTimezone = tz; }
    bool  hasTransitLocation() const { return !_transitLocation.isNull(); }

    // Transit pattern (per-tab state for the pattern input field)
    const QString& getTransitPattern() const { return _transitPattern; }
    void  setTransitPattern(const QString& pat) { _transitPattern = pat; }

    A::FileInput fileInputData() const { return { type, scope.inputData }; }
    A::FileInput fileInputData(FileType typ) const
    {
        return { typ, scope.inputData };
    }

    const A::PlanetSet& focalPlanets() const { return _focalPlanets; }

    void calculate() { recalculate(); }

    /// Force a full recompute + notify, even when no field changed. Used to
    /// "catch up" handlers that were suppressed during scrubbing (wheel drag /
    /// animation): unlike setGMT(), this fires changed() with the GMT flag
    /// regardless of whether the GMT value actually differs.
    void forceRecalculate() { change(GMT); }

    const A::InputData& data() const { return scope.inputData; }

    // Events data and model
    A::HarmonicEvents& events() { return _evs; }
    const A::HarmonicEvents& events() const { return _evs; }
    
    QAbstractItemModel* eventsModel();
    void setEventsModel(QAbstractItemModel* model);
    void clearEventsModel();
    void clearEvents() { _evs.clear(); }
    
    bool needsEventsRecalc() const { return _eventsNeedRecalc; }
    void markEventsForRecalc() { _eventsNeedRecalc = true; clearManifest(); }
    void clearEventsRecalcFlag() { _eventsNeedRecalc = false; }
    
    // PSSR context access
    const A::PSSRContext& pssrContext() const { return _pssrContext; }
    bool hasPSSRContext() const { return _pssrContextValid; }
    void setPSSRContext(const A::PSSRContext& ctx) {
        _pssrContext = ctx;
        _pssrContextValid = ctx.isValid;
    }
    void clearPSSRContext() {
        _pssrContext = A::PSSRContext();
        _pssrContextValid = false;
    }

    static void addChartDir(const QString& label, const QString& dir);

    static QMap<QString, QString>& _fixedChartDirMap();

    static const QMap<QString, QString>& fixedChartDirMap()
    {
        return _fixedChartDirMap();
    }

    static QStringList& _fixedChartDirMapKeys();

    static const QStringList& fixedChartDirMapKeys()
    {
        return _fixedChartDirMapKeys();
    }

    static QString fixedChartDir(int i = 0)
    {
        const auto& cd(fixedChartDirMapKeys());
        if (i >= 0 && i < cd.count()) {
            return fixedChartDirMap().value(cd.at(i));
        }
        return ".";
    }

  signals:
    void changed(AstroFile::Members);
    void destroyRequested();

  public slots:
    void destroy();

  private:
    bool       _unsavedChanges;
    bool       _holdUpdate;
    Members    _holdUpdateMembers;
    static int counter;

    AFileInfo    _fileInfo;
    QString      comment;
    QString      locationName;
    FileType     type;
    bool         _timezoneLocked;
    A::Horoscope scope;

    // Midpoint-composite sources (TypeComposite only)
    bool         _hasCompositeSources = false;
    AFileInfo    _compositeFiles[2];
    A::InputData _compositeInputs[2];

    // Transit date range per-tab state
    QDate   _transitStartDate;
    QString _transitDuration;

    // Transit location per-tab state (survives file-2 close)
    QVector3D _transitLocation;
    QString   _transitLocationName;
    short     _transitTimezone = 0;

    // Transit pattern per-tab state (pattern input field text)
    QString _transitPattern;

    A::PlanetSet _focalPlanets;

    // Events storage
    A::HarmonicEvents _evs;
    QAbstractItemModel* _evm = nullptr;
    bool _eventsNeedRecalc = false;
    
    // Per-file event type filter for Transits view
    A::EventTypeSet _transitEventOptions;

    // Per-file, display-only heliacal apparition phase selection (bit mask of
    // HeliacalPhaseBit). Controls which phases decompose into their own rows.
    unsigned _heliacalPhaseMask = kHeliacalPhaseDefault;

    // Per-file skip-by-duration level for Transits view
    A::EventOptions::skipper _transitSkipByDuration = A::EventOptions::SkipNone;

    // Per-file auto-reconcile (auto-refresh) preference for Transits view.
    // When false, events are recomputed only on explicit Refresh.
    bool _transitAutoReconcile = true;

    // Originating event type (e.g. etcSolarReturn) for chart preset lookup
    A::EventType _originEventType = A::etcUnknownEvent;

    // Paran group (populated when TypeParan chart is created from an event click)
    QVector<ParanGroupEntry> _paranGroupPlanets;

    // Paran cycling: per-day in-orb moments (+ orb°) of the focal cluster,
    // copied from the clicked paran event. Transient; not saved.
    QVector<QPair<QDateTime, qreal>> _paranOccurrences;

    // Optional per-occurrence phase labels (apparition stops), 1:1 with
    // _paranOccurrences. Empty for parans. Transient; not saved.
    QStringList _paranOccurrenceLabels;

    // Aspect Range Navigator (continuous animation): the in-orb range and exact
    // moment of the ranged event this chart represents (T=T/T=N/OT=N/P=P/P=N/
    // IP=N, returns…). Empty/invalid for charts that aren't a ranged event.
    // Transient; not saved.
    A::ADateTimeRange _aspectRange;
    QDateTime         _aspectExact;

    // Draw-context captured at event-click time so the navigator/animation can
    // reproduce the same chart-aspect rendering at any moment (focalExpand is a
    // transient global that reverts after the click; pattern events (TA/TNA)
    // need focalExpand=false + a specific override aspect set). Harmonic is
    // already persisted on the horoscope. Defaults = normal full-aspect drawing.
    bool           _drawFocalExpand       = false;
    A::AspectSetId _drawOverrideAspectSet = -1;

    // Per-event-type harmonic restrictions (event type → max harmonic)
    QMap<A::EventType, unsigned> _transitHarmonicRestrictions;
    
    // PSSR (Progressed Sidereal Solar Return) cache
    A::PSSRContext _pssrContext;
    bool _pssrContextValid = false;

    virtual void recalculate();
    void         recalculateBaseChart();
    void         recalculateHarmonics();
    void         change(AstroFile::Members, bool affectChangedState = true);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(AstroFile::Members)
typedef QList<AstroFile*> AstroFileList;
typedef QList<AstroFile::Members> MembersList;


/* =========================== ABSTRACT FILE HANDLER ================================ */

class AstroFileHandler : public QWidget, public Customizable {
    Q_OBJECT

  private:
    AstroFileList f;
    bool          delayUpdate;
    MembersList   delayMembers;     // accumulated data flags
    MembersList   delayViewMembers; // accumulated view-setting flags

    /// Synthetic Planet objects for midpoints in focal aspect calculation.
    /// Lifetime must span the aspect drawing cycle.
    /// std::deque (not QList/QVector) is required: we store pointers to its
    /// elements (&...back()) into the aspect planet maps while still appending,
    /// and deque guarantees those pointers stay valid across push_back. A
    /// contiguous container would reallocate and dangle them -> bad_alloc when
    /// the corrupt Planet's QString name is later rendered.
    std::deque<A::Planet> _syntheticMidpointPlanets;
    /// Midpoint ChartPlanetIds found in the current focal set.
    QList<A::ChartPlanetId> _focalMidpoints;

    /// PV-display biwheel support: pvPos is a local-frame quantity, so when
    /// chart aspects are computed in prime-vertical mode the two files'
    /// values live in different frames (natal vs transit RAMC).  These hold
    /// copies of the non-reference file's planets with pvPos re-projected
    /// into the reference file's frame (the same relocalization the chart
    /// wheel draws with), so aspect matching agrees with drawn positions.
    /// Members (not locals) because returned aspect lists keep Planet*.
    A::PlanetMap _pvRelocPlanets, _pvRelocPlanetsOrig;
    int          _pvRelocFileIndex = -1; ///< file relocalized, -1 = inactive
    int          _pvRefFileIndex   = 0;  ///< frame anchor (chart circleStart)

    /// Rebuild _pvRelocPlanets[Orig] for the current aspectMode/file set.
    /// Active (returns true) only in PV aspect mode with 2+ files.
    bool preparePvRelocalization();
    /// file(fid) planet for aspect math: the PV-relocalized copy when
    /// active for fid, else the horoscope's own planet.
    const A::Planet* aspectPlanet(int fid, A::PlanetId pid);

    MembersList blankMembers();
    bool        isAnyFileSuspended(); // returns true if any file has
                                      // isSuspendedUpdate() == true
    void dispatchUpdate(const MembersList& dataFlags,
                        const MembersList& viewFlags);

  private slots:
    void fileUpdatedSlot(AstroFile::Members);
    void fileDestroyedSlot();
    void displaySettingsSlot(int flags);

  protected:
    virtual void filesUpdated(MembersList members) = 0;

    /// Override to handle view-setting changes separately from file-data
    /// changes. Default implementation merges into filesUpdated() for
    /// backward compatibility.
    virtual void viewSettingsUpdated(MembersList members)
    {
        filesUpdated(members);
    }

    virtual void showEvent(QShowEvent* e)
    {
        QWidget::showEvent(e);
        resumeUpdate();
    }

  signals:
    void requestHelp(QString tag);

  public:
    AstroFileHandler(QWidget* parent = nullptr);
    A::AspectList calculateAspects();
    A::AspectList calculateSynastryAspects();

    /// Which file's frame anchors PV-mode synastry aspect math (the chart
    /// wheel sets this from circleStart so aspects match drawn positions).
    /// -1 disables relocalization (wheel draws both files' raw pvPos).
    void setPvFrameFile(int idx) { _pvRefFileIndex = idx; }

    /// Return the set of midpoint ChartPlanetIds in the current focal set
    /// (populated after calculateAspects/calculateSynastryAspects).
    const QList<A::ChartPlanetId>& focalMidpoints() const { return _focalMidpoints; }

    void resumeUpdate();
    void setFiles(const AstroFileList& files);

    AstroFile* file(int index = 0) const
    {
        return (f.count() > index) ? f[index] : nullptr;
    }

    AstroFileList files() const { return f; }
    int           filesCount() const { return f.count(); }
};

/* =========================== ASTRO TREE VIEW ====================================== */

/*class AstroTreeView : public QTreeWidget
{
    Q_OBJECT

    public:
        enum Topics { Topic_PersonalLife,
                      Topic_MarriageAndChildren,
                      Topic_Health,
                      Topic_Financial };

        static QList<Topics> getTopics();
        static QStringList getTopicNames();

        AstroTreeView(QWidget* parent = 0);
        void setTopic(Topics topic);
        void setFile(AstroFile* file);
        bool isEmpty()                   { return topLevelItemCount(); }

    protected:
        virtual void showEvent(QShowEvent*) { if (updateIfVisible) updateItems(); }

    private:
        bool updateIfVisible;

        Topics topic;
        AstroFile* file;

        void addTopLevelItem ( const QString& text );
        void addChildItem    ( const QString& text, bool active );
        void updateItems();

        void addPersonalLifeItems();
        void addMarriageItems();
        void addHealthItems();
        void addFinancialItems();

        const A::Planet& getMarriageSignificator ( AstroFile* file );
        bool hasDamage         ( const A::Planet& planet, const A::Horoscope &scope );
        bool hasHarmonicAspects( const A::Planet& planet, const A::Horoscope &scope );
};*/


/* =========================== ASTRO TOPICS SHOW ==================================== */

/*class AstrotTopicsShow : public AstroFileHandler
{
    Q_OBJECT

    private:
        QTabWidget* tabs;

    protected:                            // AstroFileHandler && other implementations
        void resetToDefault();
        void fileUpdated(AstroFile::Members);
        void fileDestroyed()  { }

    public:
        AstrotTopicsShow(QWidget *parent = 0);

};*/


#endif // ASTRO_GUI_H
