#ifndef TRANSITS_H
#define TRANSITS_H

#include "astro-data.h"
#include "astrodatetimeedit.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QHash>
#include <QModelIndex>
#include <QPointer>
#include <QTreeView>


#include <Astroprocessor/Gui>

class QTreeView;
class QStandardItemModel;
class QRadioButton;
class GeoSearchWidget;
class EventsTableModel;
class EventTypeFilterProxy;
class AChangeSignalFrame;

class ASignalBlocker {
    QSet<QObject*> _unblock;

  public:
    ASignalBlocker(QObject* obj) { maybeBlock(obj); }

    ASignalBlocker(std::initializer_list<QObject*> objs)
    {
        for (auto obj : objs) maybeBlock(obj);
    }

    void maybeBlock(QObject* obj)
    {
        if (!obj->signalsBlocked() && !_unblock.contains(obj)) {
            obj->blockSignals(true);
            _unblock.insert(obj);
        }
    }

    ~ASignalBlocker()
    {
        for (auto obj : std::as_const(_unblock)) obj->blockSignals(false);
    }
};

class TransitTreeView : public QTreeView {
    Q_OBJECT;

  public:
    using QTreeView::QTreeView;

  signals:
    void currently(const QModelIndex&);

  protected:
    void currentChanged(const QModelIndex& now, const QModelIndex&) override
    {
        emit currently(now);
    }
};

class Transits : public AstroFileHandler {
    Q_OBJECT

  public:
    Transits(QWidget* parent = nullptr);
    ~Transits();

    QTreeView* ttv() const;
    void       stopThreads();
    void       setInhibitUpdate(bool v) { _inhibitUpdate = v; }
    void       refreshLocationUI();

  protected: // AstroFileHandler implementation
    void filesUpdated(MembersList) override;
    void viewSettingsUpdated(MembersList) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

    AppSettings defaultSettings() override;
    AppSettings currentSettings() override;
    void        applySettings(const AppSettings&) override;
    void        setupSettingsEditor(AppSettingsEditor*) override;

    void describePlanet();
    void clear();

    EventsTableModel* tvm() const;

    EventsTableModel* ensureEventsModel();

    bool transitsOnly() const;

    AstroFile* transitsAF();

  signals:
    // void updateTransits(double);
    void planetSelected(A::PlanetId, int);
    void needToFindIt(const QString&);
    // void addChart(const A::InputData&);
    // void completed();

    void updateFirst(AstroFile*);
    void updateSecond(AstroFile*);
    void addChart(AstroFile*);
    void addChartWithTransits(const AFileInfo&, AstroFile*);

    void updateHarmonics(double);

    void cancelActive();
    void pauseActive();
    void resumeActive();

  protected slots:
    void updateTimezone();
    void onEventSelectionChanged();
    void onDateRangeChanged();

    void updateDelta(const QDate&);

    void onStartChanged();
    void onStartChanged(const QDate&)
    {
        if (!_start->hasFocus()) onStartChanged();
    }

    void onEndChanged();
    void onEndChanged(const QDate&)
    {
        if (!_end->hasFocus()) onEndChanged();
    }

    void onDurationChanged();
    void onDurationChanged(const QString&)
    {
        if (!_duration->hasFocus()) onDurationChanged();
    }

    void updateTransits();
    void onProgress(double prog);
    void onCompleted();
    void clickedCell(QModelIndex);
    void doubleClickedCell(QModelIndex);
    void headerDoubleClicked(int);
    void headerClicked(int);
    void copySelection();
    void copyTableAsRichText();
    void findIt(const QString&);
    void saveScrollPos();
    void restoreScrollPos();

  public slots:
    void setCurrentPlanet(A::PlanetId, int);
    void onLocationChange();

  private:
    A::PlanetId _planet;
    int         _fileIndex;
    bool        _expandedAspects;
    bool        _inhibitUpdate;
    bool        _pendingLocationChange = false;
    bool        _fileJustSwitched = false;  // Set in filesUpdated when file(0) changes, cleared in viewSettingsUpdated
    AstroFile*  _previousFile = nullptr;    // Tracks file(0) across tab switches

    QPointer<AstroFile> _trans;

    TransitTreeView* _tview;

    // Per-file finder tracking: each tab's finder thread lives here
    struct FinderState {
        QPointer<QThread>           thread;
        QPointer<A::AspectFinder>   finder;
        AChangeSignalFrame*         chs = nullptr;

        // Manifest metadata: what this finder was asked to search for.
        // Recorded at launch, read at completion to populate EventStore.
        A::EventTypeSet             searchedTypes;
        A::ADateRange               searchedRange;
        A::uintSSet                 searchedHarmonics;
        unsigned                    searchedSkip = 0;  // skip-by-duration level used
    };
    QHash<AstroFile*, FinderState> _finders;   // All active/paused finders

    // Current-tab aliases (convenience pointers, always match _finders[file(0)] when running)
    QPointer<QThread>           _active;
    QPointer<A::AspectFinder>   _activeFinder;
    AChangeSignalFrame*         _chs = nullptr;

    bool              _pendingRestart = false;  // Set when old thread is canceling and we need to restart
    bool              _inUpdateTransits = false; // Re-entrancy guard for updateTransits()
    bool              _backgroundFinders = false; // When true, finders keep computing on tab switch instead of pausing
    QComboBox*        _input;
    QString           _lastUsedPattern;   // pattern text used for cached events
    A::AstroDateTimeEdit* _start;
    QLineEdit*        _duration;
    QButtonGroup*     _grp;
    QRadioButton*     _endRB;
    QRadioButton*     _duraRB;
    QPushButton*      _back;
    QPushButton*      _forth;
    A::AstroDateTimeEdit* _end;

    enum class AnchorType {
        None,
        Top,       // Preserve top visible row (user scrolled down)
        Selection, // Preserve selected row at its visual offset (user clicked)
        Bottom     // Preserve bottom visible row (user scrolled up)
    };

    struct ScrollAnchor {
        A::HarmonicEvent event;
        AnchorType type = AnchorType::None;
        int sortColumn = -1;
        Qt::SortOrder sortOrder = Qt::AscendingOrder;
        int visibleRowOffset = -1; // Offset from viewport top (for Selection type)
        
        void clear() {
            event = A::HarmonicEvent();
            type = AnchorType::None;
            sortColumn = -1;
            sortOrder = Qt::AscendingOrder;
            visibleRowOffset = -1;
        }
        
        bool isValid() const { return type != AnchorType::None; }
    };

    ScrollAnchor _anchor;
    bool         _inRestoreScrollPos = false;
    int          _lastScrollValue = -1; // Track scroll direction

    GeoSearchWidget* _location;

    QPointer<EventsTableModel> _evm;  // Points to file(0)->eventsModel() - QPointer for safe destruction
    EventTypeFilterProxy*      _filterProxy = nullptr;  // Sits between _evm and _tview for event-type filtering
    
    ADateDelta _ddelta;

    QTimer* _progressSortTimer = nullptr;  // Debounces progress-triggered sorts
    
    // Per-tab event visibility state (which event types are enabled in THIS specific tab)
    // Managed exclusively by toolbar actions - options dialog does NOT modify this
    // Options dialog only sets EventOptions::globalDefaults() for newly opened tabs
    // Global settings (orbs, skipByDuration, etc.) are read from EventOptions::current()
    A::EventTypeSet _tabEventOptions;

    // Per-tab skip-by-duration level (mirrors AstroFile::_transitSkipByDuration).
    // SkipNone means "show all"; the duration toolbar button toggles between
    // SkipNone and _lastSkipLevel.
    A::EventOptions::skipper _tabSkipByDuration = A::EventOptions::SkipNone;
    // Remembered level for the duration button when toggled off, so re-enabling
    // restores the user's last choice rather than defaulting.
    A::EventOptions::skipper _lastSkipLevel = A::EventOptions::SkipLessThanWeek;

    // When true (default), changes reconcile (recompute/filter) immediately.
    // When false, changes accumulate and the Refresh button lights up instead.
    bool _autoReconcile = true;

    // Toolbar actions for event filters
    QAction* _actStations = nullptr;
    QAction* _actReturns = nullptr;
    QAction* _actTransitToTransit = nullptr;
    QToolButton* _btnTransitToNatal = nullptr;  // Dropdown: T=N / OT=N
    QAction* _actTransitToNatal = nullptr;      // T=N radio button in menu
    QAction* _actOuterTransitToNatal = nullptr; // OT=N radio button in menu
    QAction* _actIncludeAngles = nullptr;       // Menu item in T=N dropdown
    QAction* _actProgressedToProgressed = nullptr;
    QToolButton* _btnProgressedToNatal = nullptr;  // Dropdown: IP=N / P=N
    QAction* _actInnerProgressedToNatal = nullptr; // IP=N radio button in menu
    QAction* _actAllProgressedToNatal = nullptr;   // P=N radio button in menu
    QAction* _actTransitAspectPatterns = nullptr;
    QAction* _actTransitNatalAspectPatterns = nullptr;
    QAction* _actSignIngress = nullptr;
    QAction* _actHouseIngress = nullptr;
    QAction* _actParanatellonta = nullptr;
    QAction* _actParanatellontaToNatal = nullptr;

    // Skip-by-duration split button (1d / 1w / 1m), modeled on the T=N button
    QToolButton* _btnSkipDuration = nullptr;
    QAction* _actSkip1d = nullptr;
    QAction* _actSkip1w = nullptr;
    QAction* _actSkip1m = nullptr;

    // Refresh / auto-reconcile split button (replaces the old auto-recalc toggle).
    // Checkable: the checked highlight indicates pending (stale) changes; the
    // dropdown holds the "Auto" toggle.
    QToolButton* _btnRefresh = nullptr;
    QAction* _actAuto = nullptr;         // dropdown: toggle auto-reconcile
    
    bool _transitToNatalShowsOuter = false;  // Track T=N vs OT=N state
    bool _progressedToNatalShowsInner = true;  // Track P=N vs IP=N state (default inner)
    bool _transitToNatalAnglesWasChecked = true;  // Cache angles checkbox state when button is off
    
    QString _inputBorderStyle;  // Current green/red validation border for _input
    double  _lastProgress = -1; // Last progress value for throttling stylesheet updates

    void updateToolbarFromEventOptions();
    void updateTransitToNatalButtonState();
    void updateProgressedToNatalButtonState();
    void updateInputProgress(double prog);  // Update _input background to show progress
    void disconnectFinder(FinderState& fs);         // Disconnect UI signals from a finder
    void cancelAndRemoveFinder(AstroFile* af);       // Cancel finder + remove from map

    /// Launch a scoped finder for only the specified event types.
    /// Appends results to the existing event list (no clear).
    void launchScopedFinder(const A::EventTypeSet& types);

    /// Current date range of the view ([_start, _end]).
    A::ADateRange currentRange() const;

    /// Event types not adequately cached for the current types/range/skip —
    /// derived from the EventStore manifest.  Empty in pattern mode (the
    /// pattern finder computes its own set; the proxy accepts all rows).
    A::EventTypeSet desiredStale() const;

    /// True when a recompute would change the displayed events: either a hard
    /// invalidation is pending (needsEventsRecalc) or the manifest doesn't
    /// cover the desired types/range/skip.
    bool needsRefresh() const;

    /// Bring the displayed events in line with the desired settings.  When
    /// auto-reconcile is off, just updates the Refresh button's lit state.
    void reconcile();

    /// Enable/highlight the Refresh button iff needsRefresh().
    void updateRefreshButtonState();

    /// Persist tab options to file(0), update the proxy filter, then reconcile.
    /// `added` == true means an event type was just enabled, which always forces
    /// a recompute (some types only compute as part of the full scan, so we can't
    /// rely on manifest-derived staleness here).  `added` == false (a type was
    /// disabled) is filter-only — the proxy hides the rows, no recompute.
    /// Replaces the former saveEventOptionsAndRecalc lambda.
    void saveEventOptionsAndReconcile(bool added);

    /// Apply a new skip-by-duration level: update proxy (instant hide/show),
    /// persist, and reconcile (relaxing may recompute; tightening is filter-only).
    void applySkipByDuration(A::EventOptions::skipper s);

    /// Sync the duration split button's checked state and label from _tabSkipByDuration.
    void updateSkipDurationButton();
};

#endif // Harmonics_H
