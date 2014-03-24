#ifndef ASTRO_GUI_H
#define ASTRO_GUI_H

#include "astro-data.h"
#include "appsettings.h"
#include <QDebug>


/* =========================== ASTRO FILE =========================================== */

class AstroFile : public QObject
{
    Q_OBJECT

    public:
        enum FileType { TypeMale    = 1,
                        TypeFemale  = 2,
                        TypeOther   = 0 };

        enum Member { None          = 0x0,
                      Name          = 0x1,
                      Type          = 0x2,
                      GMT           = 0x4,
                      Timezone      = 0x8,
                      Location      = 0x10,
                      LocationName  = 0x20,
                      Comment       = 0x40,
                      HouseSystem   = 0x80,
                      Zodiac        = 0x100,
                      AspectLevel   = 0x200,
                      ChangedState  = 0x400,
                      All           = 0xFFF };

        Q_DECLARE_FLAGS(Members, Member)

        AstroFile(QObject* parent = 0);

        void save();
        void load(QString name);
        QString fileName() const;

        QString typeToString(FileType type);
        FileType typeFromString(QString str);

        void suspendUpdate()                     { holdUpdate = true; }
        bool isSuspendedUpdate()           const { return holdUpdate; }
        void resumeUpdate();
        void clearUnsavedState();
        bool hasUnsavedChanges()           const { return unsavedChanges; }
        bool isEmpty()                     const { return scope.planets.count() == 0; }

        void setName         (const QString&   name);
        void setType         (const FileType   type);
        void setGMT          (const QDateTime& gmt);
        void setTimezone     (const short& zone);
        void setLocation     (const QVector3D  location);
        void setLocationName (const QString&   location);
        void setComment      (const QString&   comment);
        void setHouseSystem  (A::HouseSystemId system);
        void setZodiac       (A::ZodiacId zod);
        void setAspectLevel  (A::AspectLevel lev);

        const QString&   getName()         const { return name; }
        const QString&   getComment()      const { return comment; }
        FileType         getType()               { return type; }
        const QVector3D& getLocation()     const { return scope.inputData.location; }
        const QString&   getLocationName() const { return locationName; }
        const QDateTime& getGMT()          const { return scope.inputData.GMT; }
        const short&     getTimezone()     const { return timezone; }
        const A::Horoscope& horoscope()   const { return scope; }
        A::HouseSystemId getHouseSystem() const { return scope.inputData.houseSystem; }
        A::ZodiacId      getZodiac()      const { return scope.inputData.zodiac; }
        A::AspectLevel   getAspetLevel() const { return scope.inputData.level; }
        QDateTime        getLocalTime()    const { return scope.inputData.GMT.addSecs(timezone * 3600); }

    signals:
        void changed(AstroFile::Members);

    public slots:
        void destroy();

    private:
        bool unsavedChanges;
        bool holdUpdate;
        Members holdUpdateMembers;
        static int counter;

        QString name;
        short timezone;
        QString comment;
        QString locationName;
        FileType type;
        A::Horoscope scope;

        void change(AstroFile::Members);
        void recalculate();

};

Q_DECLARE_OPERATORS_FOR_FLAGS(AstroFile::Members)


/* =========================== ABSTRACT FILE HANDLER ================================ */

class AstroFileHandler : public QWidget, public Customizable
{
    Q_OBJECT

    private:
        AstroFile* f;
        bool delayUpdate;
        AstroFile::Members delayMembers;

    private slots:
        void fileUpdatedSlot(AstroFile::Members m);
        void fileDestroyedSlot()                      { f = 0; resetToDefault(); fileDestroyed(); }

    protected:
        virtual void resetToDefault() = 0;                  // reset GUI values
        virtual void fileUpdated(AstroFile::Members) = 0;   // display file changes
        virtual void fileDestroyed()  = 0;

        virtual void showEvent(QShowEvent*);

    signals:
        void requestHelp(QString tag);

    public:
        AstroFileHandler(QWidget *parent = 0);

        void setFile (AstroFile* file);
        AstroFile* file()                             { return f; }
};


/* =========================== ASTRO TREE VIEW ====================================== */

/*class AstroTreeView : public QTreeWidget
{
    Q_OBJECT

    public:
        enum Topics { Topic_PersonalLife,
                      Topic_MarriageAndChildren,
                      Topic_Health,
                      Topic_Financial };

        static QList<Topics> getTopics();
        static QStringList getTopicNames();

        AstroTreeView(QWidget* parent = 0);
        void setTopic(Topics topic);
        void setFile(AstroFile* file);
        bool isEmpty()                   { return topLevelItemCount(); }

    protected:
        virtual void showEvent(QShowEvent*) { if (updateIfVisible) updateItems(); }

    private:
        bool updateIfVisible;

        Topics topic;
        AstroFile* file;

        void addTopLevelItem ( const QString& text );
        void addChildItem    ( const QString& text, bool active );
        void updateItems();

        void addPersonalLifeItems();
        void addMarriageItems();
        void addHealthItems();
        void addFinancialItems();

        const A::Planet& getMarriageSignificator ( AstroFile* file );
        bool hasDamage         ( const A::Planet& planet, const A::Horoscope &scope );
        bool hasHarmonicAspects( const A::Planet& planet, const A::Horoscope &scope );
};*/


/* =========================== ASTRO TOPICS SHOW ==================================== */

/*class AstrotTopicsShow : public AstroFileHandler
{
    Q_OBJECT

    private:
        QTabWidget* tabs;

    protected:                            // AstroFileHandler && other implementations
        void resetToDefault();
        void fileUpdated(AstroFile::Members);
        void fileDestroyed()  { }

    public:
        AstrotTopicsShow(QWidget *parent = 0);

};*/


#endif // ASTRO_GUI_H
