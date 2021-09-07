#ifndef FILEEDITOR_H
#define FILEEDITOR_H

#include <Astroprocessor/Gui>
#include <QRegularExpression>
#include <QComboBox>

class QLabel;
class QLineEdit;
class QListWidget;
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
    QDateTimeEdit* dateTime;
    QDoubleSpinBox* timeZone;
    GeoSearchWidget* geoSearch;
    QPlainTextEdit* comment;
    QLabel* startDateLbl;
    QDateEdit* startDate;
    QDateEdit* endDate;
    QCheckBox* endDateCB;
    QListWidget* hits;

    QRegularExpression _re, _zposre;

    bool _inUpdate;
    bool _inApply;
    bool _inDateSelection;

    static QStringList planets, signs, events;

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
    void hasSelection(bool);

public slots:
    virtual void applyToFile(bool setNeedsSave=true,
                             bool resume=true);
    void onEditingFinished();
    void setType(FileType t) { type->setCurrentIndex(unsigned(t)); }

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

#endif // FILEEDITOR_H
