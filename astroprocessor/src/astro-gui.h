#ifndef ASTRO_GUI_H
#define ASTRO_GUI_H

#include <QStringList>
#include <QVariant>
#include <QMetaType>
#include <QTimeZone>

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

    // --- setters (emit changed() when value differs) ---
    void setHouseSystem(A::HouseSystemId);
    void setZodiac(A::ZodiacId);
    void setAspectSet(A::AspectSetId, bool force = false);
    void setAspectMode(A::aspectModeEnum);

    /// Batch-update: set all four at once, emit a single changed() signal
    /// with the union of flags.  Used by toolbar / setupFile.
    void apply(A::HouseSystemId hsys,
               A::ZodiacId     zod,
               A::AspectSetId  aset,
               A::aspectModeEnum mode,
               bool forceAspect = false);

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

        // --- Group masks ---
        FileData     = Name | Type | GMT | Timezone | Location
                       | LocationName | Comment,
        ViewSettings = HouseSystem | Zodiac | AspectSet | AspectMode
                       | Harmonic,
        All          = 0x1FFF
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
    void loadComposite(const AFileInfoList& names);

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
    void setEventList(const QList<QDateTime>& evl);
    void setDateRange(const ADateRange& startEnd) { _dateRange = startEnd; }
    void setHarmonic(double harmonic);
    void setBaseChart(const QDateTime& baseGmt);
    void clearBaseChart();
    void setTimezoneLocked(bool locked);

    void setFocalPlanets(const A::PlanetSet& fp = {}) { _focalPlanets = fp; }
    
    const A::EventTypeSet& getTransitEventOptions() const { return _transitEventOptions; }
    void setTransitEventOptions(const A::EventTypeSet& opts);

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
    const QList<QDateTime>& getEventList() const { return _eventList; }
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
        return getGMT().toTimeZone(
            QTimeZone(static_cast<int>(getTimezone() * 3600)));
    }

    A::CalendarType getCalendarType() const
    {
        return scope.inputData.calendarType();
    }
    A::TimeMode getTimeMode() const { return scope.inputData.timeMode(); }

    const ADateRange& getDateRange() const { return _dateRange; }

    // Transit date range (per-tab state for transits view)
    QDate getTransitStartDate() const { return _transitStartDate; }
    void setTransitStartDate(const QDate& date) { _transitStartDate = date; }
    QString getTransitDuration() const { return _transitDuration; }
    void setTransitDuration(const QString& duration) { _transitDuration = duration; }

    A::FileInput fileInputData() const { return { type, scope.inputData }; }
    A::FileInput fileInputData(FileType typ) const
    {
        return { typ, scope.inputData };
    }

    const A::PlanetSet& focalPlanets() const { return _focalPlanets; }

    void calculate() { recalculate(); }

    const A::InputData& data() const { return scope.inputData; }

    // Events data and model
    A::HarmonicEvents& events() { return _evs; }
    const A::HarmonicEvents& events() const { return _evs; }
    
    QAbstractItemModel* eventsModel();
    void setEventsModel(QAbstractItemModel* model);
    void clearEventsModel();
    void clearEvents() { _evs.clear(); }
    
    bool needsEventsRecalc() const { return _eventsNeedRecalc; }
    void markEventsForRecalc() { _eventsNeedRecalc = true; }
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

    QList<QDateTime> _eventList; // computed contact dateTimes
    ADateRange       _dateRange; // really just start, end

    // Transit date range per-tab state
    QDate   _transitStartDate;
    QString _transitDuration;

    A::PlanetSet _focalPlanets;

    // Events storage
    A::HarmonicEvents _evs;
    QAbstractItemModel* _evm = nullptr;
    bool _eventsNeedRecalc = false;
    
    // Per-file event type filter for Transits view
    A::EventTypeSet _transitEventOptions;
    
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
    QList<A::Planet> _syntheticMidpointPlanets;
    /// Midpoint ChartPlanetIds found in the current focal set.
    QList<A::ChartPlanetId> _focalMidpoints;

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
