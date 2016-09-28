#ifndef PLAIN_H
#define PLAIN_H

#include <Astroprocessor/Gui>

class QCheckBox;
class QTextBrowser;


/* ================================== WIDGET ======================================== */

class Plain : public AstroFileHandler
{
    Q_OBJECT

    private:
        QCheckBox* describeInput;
        QCheckBox* describePlanets;
        QCheckBox* describeHouses;
        QCheckBox* describeAspects;
        QCheckBox* describePower;
        QCheckBox* describeParans;
        QCheckBox* describeSpeculum;
        QTextBrowser* view;
        bool showAllDiurnalEvents;

    private slots:
        void refresh();

    protected:                            // AstroFileHandler implementations
        void filesUpdated(MembersList m);

        AppSettings defaultSettings();
        AppSettings currentSettings();
        void applySettings(const AppSettings &);
        void setupSettingsEditor(AppSettingsEditor*);

    public:
        Plain  ( QWidget* parent = 0 );
};


#endif // PLAIN_H
