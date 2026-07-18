#ifndef FILEEDITOR_H
#define FILEEDITOR_H

#include <Astroprocessor/Gui>
#include <QComboBox>
#include "astrodatetimeedit.h"

class QLabel;
class QLineEdit;
class QCheckBox;
class QDateTimeEdit;
class QDateEdit;
class QDoubleSpinBox;
class GeoSearchWidget;
class QPlainTextEdit;
class QTabBar;
class QTableView;


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
    QComboBox* basis;
    QLabel* basisLbl;
    A::AstroDateTimeEdit* dateTime;
    QDoubleSpinBox* timeZone;
    QPushButton* lockTimezone;
    GeoSearchWidget* geoSearch;
    QPlainTextEdit* comment;

    bool _inUpdate;
    bool _inApply;
    bool _userEditedTime;  // Track if user manually changed time in this session

    void update(AstroFile::Members);
    void updateTabs();

protected:
    void filesUpdated(MembersList members) override;  // AstroFileHandler implementations
    //virtual void showEvent(QShowEvent*);
    void closeEvent(QCloseEvent*) override { emit windowClosed(); }

signals:
    void windowClosed();
    void appendFile();
    void swapFiles(int, int);

public slots:
    virtual void applyToFile(bool setNeedsSave=true,
                             bool resume=true);
    void setType(FileType t) { type->setCurrentIndex(type->findData(unsigned(t))); }

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
    
    static QStringList signs;
};

#endif // FILEEDITOR_H
