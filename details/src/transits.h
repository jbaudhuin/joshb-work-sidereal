#ifndef TRANSITS_H
#define TRANSITS_H

#include <QButtonGroup>
#include <QDateEdit>
#include <QLineEdit>
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

  protected: // AstroFileHandler implementation
    void filesUpdated(MembersList) override;

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
    void       stopThreads();

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

    AstroFile* _trans = nullptr;

    TransitTreeView* _tview;

    QPointer<QThread> _active;
    QLineEdit*        _input;
    QDateEdit*        _start;
    QLineEdit*        _duration;
    QButtonGroup*     _grp;
    QRadioButton*     _endRB;
    QRadioButton*     _duraRB;
    QPushButton*      _back;
    QPushButton*      _forth;
    QDateEdit*        _end;

    A::HarmonicEvent _anchorTop, _anchorCur;
    int              _anchorSort;
    Qt::SortOrder    _anchorOrder;
    int              _anchorVisibleRow;
    bool             _inRestoreScrollPos = false;

    GeoSearchWidget* _location;

    EventsTableModel* _evm;  // Points to file(0)->eventsModel()
    
    ADateDelta _ddelta;

    AChangeSignalFrame* _chs;
    
    QTimer* _progressSortTimer = nullptr;  // Debounces progress-triggered sorts
};

#endif // Harmonics_H
