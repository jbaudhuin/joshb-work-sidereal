#ifndef FILEEDITOR_H
#define FILEEDITOR_H

#include <Astroprocessor/Gui>
#include <QTableView>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class GeoSearchWidget;
class QPlainTextEdit;
class QTabBar;


/* =========================== ASTRO FILE EDITOR =========================== */

class AstroFileEditor : public AstroFileHandler
{
    Q_OBJECT;

protected:
    int currentFile;

    QTabBar* tabs;
    QPushButton* addFileBtn;
    QLineEdit* name;
    QComboBox* type;
    QDateTimeEdit* dateTime;
    QDoubleSpinBox* timeZone;
    GeoSearchWidget* geoSearch;
    QPlainTextEdit* comment;

    bool _inUpdate;

    void update(AstroFile::Members);
    void updateTabs();

protected:
    void filesUpdated(MembersList members);  // AstroFileHandler implementations
    //virtual void showEvent(QShowEvent*);
    virtual void closeEvent(QCloseEvent*) { emit windowClosed(); }

signals:
    void windowClosed();
    void appendFile();
    void swapFiles(int, int);

public slots:
    virtual void applyToFile(bool setNeedsSave=true,
                             bool resume=true);

private slots:
    void swapFilesSlot(int, int);
    void currentTabChanged(int);
    void removeTab(int);
    void timezoneChanged();
    void updateTimezone();

public:
    AstroFileEditor(QWidget *parent = nullptr);
    void setCurrentFile(int index);

    bool& inUpdate() { return _inUpdate; }
};

class AstroFindEditor : public AstroFileEditor
{
    Q_OBJECT;
    
public:
    AstroFindEditor(QWidget* parent = nullptr);

signals:
    void hasSelection(bool);
    
public slots:
    void onEditingFinished();
    void applyToFile();

private:
    bool _inDateSelection;
    QDateTimeEdit* endDateTime;
    QCheckBox* endDateTimeCB;
    QTableView* hits;

    QStringList planets, signs, events;
};


#endif // FILEEDITOR_H
