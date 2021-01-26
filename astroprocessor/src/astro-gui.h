#ifndef ASTRO_GUI_H
#define ASTRO_GUI_H

#include <QStringList>
#include <QVariant>
#include <QMetaType>

#include "astro-data.h"
#include "astro-calc.h"
#include "appsettings.h"
#include "../zodiac/src/afileinfo.h"

class QStandardItemModel;

using A::ADateRange;

Q_DECLARE_METATYPE(ADateRange)

/* =========================== ASTRO FILE ================================== */

class AstroFile : public QObject
{
    Q_OBJECT;

public:
    enum FileType { TypeOther, TypeMale, TypeFemale,
                    TypeEvents, TypeSearch,
                    TypeDerivedSA, TypeDerivedProg, TypeDerivedPD,
                    TypeDerivedSearch, TypeCount };

    enum Member {
        None = 0x0,
        Name = 0x1,
        Type = 0x2,
        GMT = 0x4,
        Timezone = 0x8,
        Location = 0x10,
        LocationName = 0x20,
        Comment = 0x40,
        HouseSystem = 0x80,
        Zodiac = 0x100,
        AspectSet = 0x200,
        AspectMode = 0x400,
        Harmonic = 0x800,
        HarmonicOpts = 0x1000,
        EventList = 0x2000,
        DateRange = 0x4000,
        ChangedState = 0x8000,
        All = 0xFFFF
    };

    Q_DECLARE_FLAGS(Members, Member)

    AstroFile(QObject* parent = nullptr);
    virtual ~AstroFile() { }

    QString fileName() const;
    static QString typeToString(unsigned type);
    static FileType typeFromString(const QString& str);
    AstroFile::Members diff(AstroFile* other) const;

    void save();
    void saveAs();
    void load(const AFileInfo& name);
    void loadComposite(const AFileInfoList& names);

    void suspendUpdate()            { holdUpdate = true; }
    bool isSuspendedUpdate()  const { return holdUpdate; }
    void resumeUpdate();

    void clearUnsavedState();
    bool hasUnsavedChanges()  const { return unsavedChanges; }
    bool isEmpty()            const { return scope.planets.count() == 0; }

    void setName         (const QString&   name);
    void setFileInfo(const AFileInfo& fi) { _fileInfo = fi; }
    void setType         (const FileType   type);
    void setGMT          (const QDateTime& gmt);
    void setTimezone     (const short& zone);
    void setLocation     (const QVector3D  location);
    void setLocationName (const QString&   location);
    void setComment      (const QString&   comment);
    void setHouseSystem  (A::HouseSystemId system);
    void setZodiac       (A::ZodiacId zod);
    void setAspectSet    (A::AspectSetId set, bool force = false);
    void setAspectMode   (const A::aspectModeType& mode);
    void setEventList(const QList<QDateTime>& evl);
    void setDateRange(const ADateRange& startEnd) { _dateRange = startEnd; }
    void setHarmonic     (double harmonic);

    QString          getName()         const { return _fileInfo.baseName(); }
    const AFileInfo& fileInfo() const { return _fileInfo; }
    
    const QString&   getComment()      const { return comment; }
    FileType         getType()         const { return type; }
    const QVector3D& getLocation()     const { return scope.inputData.location; }
    const QString&   getLocationName() const { return locationName; }
    const QDateTime& getGMT()          const { return scope.inputData.GMT; }
    const short&     getTimezone()     const { return scope.inputData.tz; }
    //A::Horoscope& horoscope() { return scope; }
    const A::Horoscope& horoscope()    const { return scope; }
    A::HouseSystemId getHouseSystem()  const { return scope.inputData.houseSystem; }
    A::ZodiacId      getZodiac()       const { return scope.inputData.zodiac; }
    const A::AspectsSet& getAspectSet()  const { return A::getAspectSet(scope.inputData.aspectSet); }
    A::aspectModeEnum getAspectMode()  const { return A::aspectMode; }
    const QList<QDateTime>& getEventList() const { return _eventList; }
    double           getHarmonic()     const { return scope.inputData.harmonic; }
    QDateTime        getLocalTime()    const { return scope.inputData.GMT.addSecs(getTimezone() * 3600); }
    const ADateRange& getDateRange() const { return _dateRange; }

    void             calculate() { recalculate(); }

    const A::InputData& data() const { return scope.inputData; }

    static void      addChartDir(const QString& label,
                                 const QString& dir);

    static QMap<QString,QString>& _fixedChartDirMap();

    static const QMap<QString,QString>& fixedChartDirMap()
    { return _fixedChartDirMap(); }

    static QStringList& _fixedChartDirMapKeys();

    static const QStringList& fixedChartDirMapKeys()
    { return _fixedChartDirMapKeys(); }

    static QString fixedChartDir(int i = 0)
    {
        const auto& cd(fixedChartDirMapKeys());
        if (i >= 0 && i < cd.count()) {
            return fixedChartDirMap().value(cd.at(i));
        }
        return ".";
    }

signals:
    void changed(AstroFile::Members);
    void destroyRequested();

public slots:
    void destroy();

private:
    bool unsavedChanges;
    bool holdUpdate;
    Members holdUpdateMembers;
    static int counter;

    AFileInfo _fileInfo;
    QString comment;
    QString locationName;
    FileType type;
    A::Horoscope scope;

    QList<QDateTime> _eventList;  // computed contact dateTimes
    ADateRange _dateRange; // really just start, end

    virtual void recalculate();
    void recalculateBaseChart();
    void recalculateHarmonics();
    void change(AstroFile::Members, bool affectChangedState = true);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(AstroFile::Members)
typedef QList<AstroFile*> AstroFileList;
typedef QList<AstroFile::Members> MembersList;


/* =========================== ABSTRACT FILE HANDLER ================================ */

class AstroFileHandler : public QWidget, public Customizable
{
    Q_OBJECT

    private:
        AstroFileList f;
        bool delayUpdate;
        MembersList delayMembers;

        MembersList blankMembers();
        bool isAnyFileSuspended();                      // returns true if any file has isSuspendedUpdate() == true

    private slots:
        void fileUpdatedSlot(AstroFile::Members);
        void fileDestroyedSlot();

    protected:
        virtual void filesUpdated(MembersList members) = 0;

        virtual void showEvent(QShowEvent* e)
        { QWidget::showEvent(e); resumeUpdate(); }

    signals:
        void requestHelp(QString tag);

    public:
        AstroFileHandler(QWidget *parent = nullptr);
        A::AspectList calculateSynastryAspects();
        void resumeUpdate();
        void setFiles(const AstroFileList& files);

        AstroFile* file(int index = 0)
        { return (f.count() > index) ? f[index] : nullptr; }

        AstroFileList files() { return f; }
        int filesCount()      { return f.count(); }
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
