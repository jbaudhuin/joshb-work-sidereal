#ifndef HARMONICS_H
#define HARMONICS_H

#include <QModelIndex>
#include <Astroprocessor/Gui>

class QTreeView;
class QStandardItemModel;

class Harmonics : public AstroFileHandler
{
    Q_OBJECT

public:
    Harmonics(QWidget* parent = nullptr);
    ~Harmonics() { }

    QTreeView* htv() const { return _hview; }

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
    void updateHarmonics(double);
    void planetSelected(A::PlanetId, int);
    void needToFindIt(const QString&);

protected slots:
    void updateHarmonics();
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

    QTreeView*  _hview;

};

#endif // Harmonics_H
