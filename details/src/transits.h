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
class TransitEventsModel;
class AChangeSignalFrame;

struct ADateDelta {
    int numDays = 0;
    int numMonths = 0;
    int numYears = 0;

    ADateDelta(const QString& str);

    ADateDelta(int days = 0, int months = 0, int years = 0) :
        numDays(days), numMonths(months), numYears(years)
    { }

    ADateDelta(QDate from, QDate to);

    QString toString() const
    {
        QStringList sl;
        bool terse = numYears && numMonths && numDays;
        auto append = [&](const int& v, const QString& un) {
            if (!v) return;
            sl << (terse
                   ? QString("%1%2").arg(v).arg(un.at(0))
                   : QString("%1 %2%3").arg(v).arg(un).arg(v!=1?"s":""));
        };
        append(numYears,"yr");
        append(numMonths,"mo");
        append(numDays,"day");
        return sl.join(terse? " " : ", ");
    }

    QDate addTo(const QDate& d);
    QDate subtractFrom(const QDate& d);

    static ADateDelta fromString(const QString& str)
    { return ADateDelta(str); }

    bool operator==(const ADateDelta& other) const
    {
        return numYears == other.numYears
                && numMonths == other.numMonths
                && numDays == other.numDays;
    }

    bool operator!=(const ADateDelta& other) const
    { return !operator==(other); }

    operator bool() const { return numYears || numMonths || numDays; }

};

class ASignalBlocker {
    QSet<QObject*> _unblock;

public:
    ASignalBlocker(QObject* obj) { maybeBlock(obj); }

    ASignalBlocker(std::initializer_list<QObject*> objs)
    { for (auto obj : objs) maybeBlock(obj); }

    void maybeBlock(QObject* obj)
    {
        if (!obj->signalsBlocked()) {
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

    TransitEventsModel* tvm() const;

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
    TransitEventsModel* _evm;

    ADateDelta _ddelta;

    A::HarmonicEvents _evs;

    AChangeSignalFrame* _chs;
};

#endif // Harmonics_H
