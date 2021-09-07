#ifndef TRANSITS_H
#define TRANSITS_H

#include <QModelIndex>
#include <QButtonGroup>
#include <Astroprocessor/Gui>

class QTreeView;
class QLineEdit;
class QStandardItemModel;
class QDateEdit;
class QRadioButton;
class GeoSearchWidget;
class EventsTableModel;
class AChangeSignalFrame;

class ASignalBlocker {
    QSet<QObject*> _unblock;

public:
    ASignalBlocker(QObject* obj) { maybeBlock(obj); }

    ASignalBlocker(std::initializer_list<QObject*> objs)
    { for (auto obj : objs) maybeBlock(obj); }

    void maybeBlock(QObject* obj)
    {
        if (!obj->signalsBlocked() && !_unblock.contains(obj)) {
            obj->blockSignals(true);
            _unblock.insert(obj);
        }
    }

    ~ASignalBlocker()
    { for (auto obj : _unblock) obj->blockSignals(false); }
};

class Transits : public AstroFileHandler
{
    Q_OBJECT

public:
    Transits(QWidget* parent = nullptr);
    ~Transits() { }

    QTreeView* ttv() const { return _tview; }

protected:                            // AstroFileHandler implementation
    void filesUpdated(MembersList);

    AppSettings defaultSettings();
    AppSettings currentSettings();
    void applySettings(const AppSettings&);
    void setupSettingsEditor(AppSettingsEditor*);

    void describePlanet();
    void clear();

    EventsTableModel* tvm() const;

    bool transitsOnly() const;

    AstroFile* transitsAF();

signals:
    //void updateTransits(double);
    void planetSelected(A::PlanetId, int);
    void needToFindIt(const QString&);
    //void addChart(const A::InputData&);
    //void completed();

    void updateFirst(AstroFile*);
    void updateSecond(AstroFile*);
    void addChart(AstroFile*);
    void addChartWithTransits(const AFileInfo&,
                              AstroFile*);

    void updateHarmonics(double);

protected slots:
    void onEventSelectionChanged();
    void onDateRangeChanged();

    void updateTransits();
    void checkComplete();
    void onCompleted();
    void clickedCell(QModelIndex);
    void doubleClickedCell(QModelIndex);
    void headerDoubleClicked(int);
    void copySelection();
    void findIt(const QString&);
    void saveScrollPos();
    void restoreScrollPos();

public slots:
    void setCurrentPlanet(A::PlanetId, int);

private:
    A::PlanetId _planet;
    int _fileIndex;
    bool _expandedAspects;
    bool _inhibitUpdate;

    AstroFile* _trans = nullptr;

    QTreeView* _tview;
    QLineEdit* _input;
    QDateEdit* _start;
    QLineEdit* _duration;
    QButtonGroup* _grp;
    QRadioButton* _endRB;
    QRadioButton* _duraRB;
    QPushButton* _back;
    QPushButton* _forth;
    QDateEdit* _end;

    A::HarmonicEvent        _anchorTop, _anchorCur;
    int                     _anchorSort;
    Qt::SortOrder           _anchorOrder;
    int                     _anchorVisibleRow;

    GeoSearchWidget* _location;

    QTimer* _watcher = nullptr;

    //QStandardItemModel* _tm;
    EventsTableModel* _evm;

    ADateDelta _ddelta;

    A::HarmonicEvents _evs;

    AChangeSignalFrame* _chs;
};

#endif // Harmonics_H
