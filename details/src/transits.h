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
    
    ADateDelta _ddelta;

    QTimer* _progressSortTimer = nullptr;  // Debounces progress-triggered sorts
    
    // Per-tab event visibility state (which event types are enabled in THIS specific tab)
    // Managed exclusively by toolbar actions - options dialog does NOT modify this
    // Options dialog only sets EventOptions::globalDefaults() for newly opened tabs
    // Global settings (orbs, skipByDuration, etc.) are read from EventOptions::current()
    A::EventTypeSet _tabEventOptions;
    
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
    QAction* _actAutoRecalc = nullptr;
    
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
};

#endif // Harmonics_H
