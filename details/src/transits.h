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

    QStandardItemModel* tvm() const;

signals:
    //void updateTransits(double);
    void planetSelected(A::PlanetId, int);
    void needToFindIt(const QString&);
    void addChart(const A::InputData&);

protected slots:
    void updateTransits();
    void clickedCell(const QModelIndex&);
    void doubleClickedCell(const QModelIndex&);
    void headerDoubleClicked(int);
    void copySelection();
    void findIt(const QString&);

public slots:
    void setCurrentPlanet(A::PlanetId, int);

private:
    A::PlanetId _planet;
    int _fileIndex;
    bool _expandedAspects;
    bool _inhibitUpdate;

    QTreeView* _tview;
    QLineEdit* _input;
    QDateEdit* _start;
    QLineEdit* _duration;
    QButtonGroup* _grp;
    QRadioButton* _endRB;
    QRadioButton* _duraRB;
    QDateEdit* _end;

    QStandardItemModel* _tm;
};

#endif // Harmonics_H
