#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMetaType>
#include <QSettings>
#include <QStandardPaths>
#include <QTextCodec>
#include <QAbstractItemModel>
#include <memory>


#include "astro-calc.h"
#include "astro-gui.h"
#include <swephexp.h>
#include <Astroprocessor/Zodiac>

/* =================== DISPLAY SETTINGS ========================== */

DisplaySettings::DisplaySettings()
    : QObject(nullptr),
      _houseSystem(A::Housesystem_Placidus),
      _zodiac(A::Zodiac_Tropical),
      _aspectSet(A::AspectSet_Default)
{
}

/*static*/ DisplaySettings&
DisplaySettings::instance()
{
    static DisplaySettings s;
    return s;
}

void
DisplaySettings::setHouseSystem(A::HouseSystemId h)
{
    if (_houseSystem != h) {
        _houseSystem = h;
        emit changed(AstroFile::HouseSystem);
    }
}

void
DisplaySettings::setZodiac(A::ZodiacId z)
{
    if (_zodiac != z) {
        _zodiac = z;
        emit changed(AstroFile::Zodiac);
    }
}

void
DisplaySettings::setAspectSet(A::AspectSetId s, bool force)
{
    if (_aspectSet != s || force) {
        _aspectSet = s;
        emit changed(AstroFile::AspectSet);
    }
}

void
DisplaySettings::setAspectMode(A::aspectModeEnum m)
{
    if (A::aspectMode != m) {
        A::aspectMode = m;
        emit changed(AstroFile::AspectMode);
    }
}

void
DisplaySettings::apply(A::HouseSystemId hsys,
                       A::ZodiacId      zod,
                       A::AspectSetId   aset,
                       A::aspectModeEnum mode,
                       bool forceAspect)
{
    int flags = 0;
    if (_houseSystem != hsys) { _houseSystem = hsys; flags |= AstroFile::HouseSystem; }
    if (_zodiac != zod)       { _zodiac = zod;       flags |= AstroFile::Zodiac; }
    if (_aspectSet != aset || forceAspect)
                              { _aspectSet = aset;   flags |= AstroFile::AspectSet; }
    if (A::aspectMode != mode){ A::aspectMode = mode; flags |= AstroFile::AspectMode; }
    if (flags) emit changed(flags);
}

/* ====================== ASTRO FILE ============================= */

/*static*/ int AstroFile::counter = 0;

AstroFile::AstroFile(QObject* parent) : QObject(parent)
{
    do {
        setName(tr("Untitled %1").arg(++counter));
    } while (fileInfo().exists());

    type               = TypeOther;
    _unsavedChanges    = false;
    _holdUpdate        = false;
    _holdUpdateMembers = None;
    _timezoneLocked    = false;
    _transitTimezone     = 0;
    _transitEventOptions = A::EventOptions::globalDefaults();  // Initialize new file from global defaults
    qDebug() << "Created file" << getName();
}

QString
AstroFile::fileName() const
{
    return fileInfo().filePath();
}

QString
AstroFile::typeToString(unsigned ft)
{
    switch (ft) {
    case TypeSearch:        return "Search";
    case TypeDerivedSA:     return "SA";
    case TypeDerivedProg:   return "Prog";
    case TypeDerivedPD:     return "PD";
    case TypeDerivedSearch: return "Der";
    case TypeMale:          return "Male";
    case TypeFemale:        return "Female";
    case TypeEvent:         return "Event";
    case TypeReturn:        return "Return";
    case TypeOther:         return "Other";
    default:                break;
    }
    return "";
}

FileType
AstroFile::typeFromString(const QString& str)
{
    if (str == "Male") return TypeMale;
    if (str == "Female") return TypeFemale;
    if (str == "Der") return TypeDerivedSearch;
    if (str == "PD") return TypeDerivedPD;
    if (str == "Prog") return TypeDerivedProg;
    if (str == "SA") return TypeDerivedSA;
    if (str == "Search") return TypeSearch;
    if (str == "Event") return TypeEvent;
    if (str == "Return") return TypeReturn;
    return TypeOther;
}

AstroFile::Members
AstroFile::diff(AstroFile* other) const
{
    if (!other) return AstroFile::All;
    if (this == other) return None;

    Members flags = None;
    if (getName() != other->getName()) flags |= Name;
    if (getType() != other->getType()) flags |= Type;
    if (getGMT() != other->getGMT()) flags |= GMT;
    if (getTimezone() != other->getTimezone()) flags |= Timezone;
    if (getLocation() != other->getLocation()) flags |= Location;
    if (getLocationName() != other->getLocationName()) flags |= LocationName;
    if (getComment() != other->getComment()) flags |= Comment;
    // HouseSystem, Zodiac, AspectSet, AspectMode are global (DisplaySettings),
    // not per-file, so they don't appear in diff.
    if (getHarmonic() != other->getHarmonic()) flags |= Harmonic;
    if (hasUnsavedChanges() != other->hasUnsavedChanges())
        flags |= ChangedState;
    // lastChangedMembers = flags;
    return flags;
}

/*static*/
QMap<QString, QString>&
AstroFile::_fixedChartDirMap()
{
    static QMap<QString, QString> s_map;
    if (s_map.empty()) {
        constexpr auto loc = QStandardPaths::DocumentsLocation;
        QString dir = QStandardPaths::writableLocation(loc) + "/zodiac-charts";

        QDir d(dir);
        if (!d.exists()) QDir().mkpath(d.absolutePath());

        s_map["User Charts"]   = dir;
        s_map["Sample Charts"] = "sampleCharts/";
        _fixedChartDirMapKeys() << "User Charts" << "Sample Charts";
    }
    return s_map;
}

/*static*/
QStringList&
AstroFile::_fixedChartDirMapKeys()
{
    static QStringList s_keys;
    if (s_keys.empty()) (void) _fixedChartDirMap();
    return s_keys;
}

void
AstroFile::save()
{
    if (fileInfo().path() == ".") {
        _fileInfo.setFile(QDir(fixedChartDir()), _fileInfo.fileName());
    }
    QSettings file(fileName(), QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    file.setIniCodec(QTextCodec::codecForName("UTF-8"));
#endif

    file.setValue("name", getName());
    file.setValue("type", typeToString(getType()));
    file.setValue("GMT", getGMT().toString(Qt::ISODate));
    file.setValue("timezone", getTimezone());
    file.setValue("lon", getLocation().x());
    file.setValue("lat", getLocation().y());
    file.setValue("z", getLocation().z());
    file.setValue("placeTag", getLocationName());
    file.setValue("timezoneLocked", _timezoneLocked);
    file.setValue("comment", getComment());

    // Calendar type & time mode (new fields — old versions will simply ignore)
    if (getCalendarType() != A::Cal_Auto)
        file.setValue("calendarType", static_cast<int>(getCalendarType()));
    else
        file.remove("calendarType"); // omit default to keep files clean
    if (getTimeMode() != A::Time_ZoneTime)
        file.setValue("timeMode", static_cast<int>(getTimeMode()));
    else
        file.remove("timeMode");

    // Base chart support for progressed charts
    if (hasBaseChart()) {
        file.setValue("baseChartGMT", getBaseChartGMT().toString(Qt::ISODate));
    } else {
        file.remove("baseChartGMT");
    }

    // if (getType()==TypeEvents) {
    file.setValue("dateRange", getDateRange().operator QVariant());
    if (_eventList.empty()) {
        file.setValue("eventList", QVariant());
    } else {
        QVariantList vl;
        for (const auto& dt : std::as_const(_eventList)) {
            vl << dt;
        }
        file.setValue("eventList", vl);
    }
    //}

    // Save per-file transit event options using brief strings for readability
    if (_transitEventOptions.empty()) {
        file.setValue("transitEventOptions", QVariant());
    } else {
        QStringList sl;
        for (const auto& et : std::as_const(_transitEventOptions)) {
            sl << A::EventTypeManager::eventTypeToBrief(et);
        }
        file.setValue("transitEventOptions", sl);
    }

    qDebug() << "Saved" << getName() << "to" << fileName();

    clearUnsavedState();
}

void
AstroFile::saveAs()
{
    QString dir = fileInfo().path();
    if (dir == ".") {
        dir = fixedChartDir();
    }

    QString suggestedName = AFileInfo(dir, getName()).filePath();
    QString newPath = QFileDialog::getSaveFileName(
        nullptr,
        tr("Save Chart As"),
        suggestedName,
        tr("Chart Files (*%1)").arg(AFileInfo::suff()));

    if (newPath.isEmpty()) {
        return; // User cancelled
    }

    // Remove the suffix if user added it (AFileInfo will add it)
    if (newPath.endsWith(AFileInfo::suff())) {
        newPath.chop(AFileInfo::suff().length());
    }

    AFileInfo newFileInfo(newPath);
    QString newName = newFileInfo.baseName();

    qDebug() << "Saving as:" << newName << "to" << newFileInfo.filePath();

    // Update the file info and name
    _fileInfo = newFileInfo;
    
    // Save to the new location
    save();
    
    // Notify that the file has been renamed/moved (without marking as unsaved since we just saved)
    emit changed(Name | ChangedState);
}

void
AstroFile::load(const AFileInfo& fi /*, bool recalculate*/)
{
    QString name = fi.baseName();
    if (name.isEmpty()) return;
    qDebug() << "Overwriting" << getName() << "from" << fi.absoluteFilePath();

    suspendUpdate();
    setFocalPlanets();
    _fileInfo = fi;

    QSettings file(fileName(), QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    file.setIniCodec(QTextCodec::codecForName("UTF-8"));
#endif

    for (const QString& val : file.allKeys()) {
        qDebug() << val << file.value(val);
    }

    // Load chart type - handle both string (new) and integer (legacy) formats
    QVariant typeValue = file.value("type");
    if (typeValue.typeId() == QMetaType::QString || typeValue.typeId() == QMetaType::QByteArray) {
        setType(typeFromString(typeValue.toString()));
    } else {
        // Legacy integer format (rare) - use value directly
        int typeInt = typeValue.toInt();
        if (typeInt >= 0 && typeInt < TypeCount) {
            qWarning() << "File" << getName() << "uses legacy integer type format:" << typeInt;
            setType(static_cast<FileType>(typeInt));
            _unsavedChanges = true; // Mark for re-saving in string format
        } else {
            qWarning() << "Invalid type value for" << getName() << "- defaulting to TypeOther";
            setType(TypeOther);
        }
    }

    auto dts = file.value("GMT").toString();
    if (!dts.endsWith('Z')) dts += 'Z';
    setGMT(QDateTime::fromString(dts, Qt::ISODate));

    setTimezone(file.value("timezone").toDouble());
    setLocation(QVector3D(file.value("lon").toFloat(),
                          file.value("lat").toFloat(),
                          file.value("z").toFloat()));
    setLocationName(file.value("placeTag").toString());
    _timezoneLocked = file.value("timezoneLocked", false).toBool();
    setComment(file.value("comment").toString());

    // Calendar type & time mode (defaults preserve backwards compatibility)
    setCalendarType(static_cast<A::CalendarType>(
        file.value("calendarType", static_cast<int>(A::Cal_Auto)).toInt()));
    setTimeMode(static_cast<A::TimeMode>(
        file.value("timeMode", static_cast<int>(A::Time_ZoneTime)).toInt()));

    // Clear cached events since we're loading a new chart
    clearEventsModel();
    _evs.clear();
    _eventsNeedRecalc = false;

    // Load base chart if present (for progressed charts)
    if (file.contains("baseChartGMT")) {
        auto baseDts = file.value("baseChartGMT").toString();
        if (!baseDts.endsWith('Z')) baseDts += 'Z';
        setBaseChart(QDateTime::fromString(baseDts, Qt::ISODate));
    } else {
        clearBaseChart();
    }

    // if (getType()==TypeEvents) {
    QList<QDateTime> dl;
    if (file.contains("eventList")) {
        auto vl = file.value("eventList").toList();
        for (const auto& v : std::as_const(vl)) {
            dl << v.toDateTime();
        }
        _eventList.swap(dl);
    }
    ADateRange range;
    if (file.contains("dateRange")) {
        range = file.value("dateRange");
    }
    setDateRange(range);
    //}

    // Load per-file transit event options (handle both old int and new string formats)
    if (file.contains("transitEventOptions")) {
        _transitEventOptions.clear();
        QVariant vopt = file.value("transitEventOptions");
        
        // Handle both old format (QVariantList of ints) and new format (QStringList)
        if (vopt.canConvert<QStringList>()) {
            // New format: strings using event brief names (e.g., "TA", "P=P")
            QStringList sl = vopt.toStringList();
            for (const auto& s : std::as_const(sl)) {
                A::EventType et = A::EventTypeManager::briefToEventType(s);
                if (et != A::etcUnknownEvent) {
                    _transitEventOptions.insert(et);
                }
            }
        } else {
            // Old format: integers (for backward compatibility)
            auto vl = vopt.toList();
            for (const auto& v : std::as_const(vl)) {
                _transitEventOptions.insert(static_cast<A::EventType>(v.toInt()));
            }
        }
    } else {
        // Default to current global settings for files saved before this feature
        _transitEventOptions = A::EventOptions::globalDefaults();
    }

    clearUnsavedState();
    if (/*!recalculate*/ !isEmpty())
        resumeUpdate() /*holdUpdateMembers = None*/; // if empty file is just
                                                     // loaded, it will not be
                                                     // recalculated
    // resumeUpdate();
}

void
AstroFile::loadComposite(const AFileInfoList& names)
{
    suspendUpdate();
    setFileInfo(names.first());

    auto file = std::make_unique<QSettings>(fileName(), QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    file->setIniCodec(QTextCodec::codecForName("UTF-8"));
#endif

    auto dts = file->value("GMT").toString();
    if (!dts.endsWith('Z')) dts += 'Z';
    setGMT(QDateTime::fromString(dts, Qt::ISODate));
}

void
AstroFile::resumeUpdate()
{
    if (!_holdUpdate) return;
    _holdUpdate = false;
    change(_holdUpdateMembers, false);
    _holdUpdateMembers = None;
}

void
AstroFile::setTransitEventOptions(const A::EventTypeSet& opts)
{
    if (_transitEventOptions != opts) {
        _transitEventOptions = opts;
        change(ChangedState, true);  // Mark as modified
        // Don't save automatically - only save when user explicitly saves the chart
        // Transit event options are preserved via session state
    }
}

void
AstroFile::change(AstroFile::Members members, bool affectChangedState)
{
    if (members == None) return;

    bool unsavedBefore = _unsavedChanges;
    if (affectChangedState && !(members & ChangedState)) _unsavedChanges = true;
    if (unsavedBefore != _unsavedChanges) members |= ChangedState;

    if (!_holdUpdate) {
        // Recalculate for file-data changes that affect ephemeris
        if (members & (Type | GMT | Location))
            recalculate();
        // Harmonic is per-file and triggers a cheaper recalc
        else if (members & Harmonic)
            recalculateBaseChart();
        // HouseSystem/Zodiac/AspectSet/AspectMode recalc is handled
        // by DisplaySettings → stampDisplaySettings()

        emit changed(members);
    } else {
        _holdUpdateMembers |= members;
    }
}

void
AstroFile::clearUnsavedState()
{
    if (hasUnsavedChanges()) {
        _unsavedChanges = false;
        if (!_holdUpdate) change(ChangedState);
        else if (_holdUpdateMembers & ChangedState)
            _holdUpdateMembers ^= ChangedState;
    }
}

void
AstroFile::setName(const QString& name)
{
    if (getName() != name) {
        qDebug() << "Renamed file" << getName() << "->" << name;
        _fileInfo.setFile(name);
        change(Name);
    }
}

void
AstroFile::setType(const FileType type)
{
    if (this->type != type) {
        qDebug() << "[PERF] setType for file" << getName() << "from" << this->type << "to" << type;
        this->type = type;
        
        // Set progression flag based on type
        // This must be done here so InputData state is stable for caching
        bool shouldProgress = (type == TypeDerivedProg || type == TypeDerivedSA || type == TypeDerivedPD);
        scope.inputData.setProgressed(shouldProgress);
        
        change(Type);
    }
}

void
AstroFile::setGMT(const QDateTime& gmt)
{
    if (scope.inputData.GMT() != gmt) {
        scope.inputData.setGMT(gmt);
        change(GMT);
    }
}

void
AstroFile::setTimezone(double zone)
{
    if (scope.inputData.tz() != zone) {
        scope.inputData.setTZ(zone);
        change(Timezone);
    }
}

void
AstroFile::setCalendarType(A::CalendarType ct)
{
    if (scope.inputData.calendarType() != ct) {
        scope.inputData.setCalendarType(ct);
        change(GMT); // recalculate — same moment, different calendar
    }
}

void
AstroFile::setTimeMode(A::TimeMode tm)
{
    if (scope.inputData.timeMode() != tm) {
        scope.inputData.setTimeMode(tm);
        change(GMT); // recalculate — interpretation of entered time changes
    }
}

void
AstroFile::setLocation(const QVector3D location)
{
    if (scope.inputData.location() != location) {
        scope.inputData.setLocation(location);
        change(Location);
    }
}

void
AstroFile::setLocationName(const QString& location)
{
    if (this->locationName != location) {
        this->locationName = location;
        change(LocationName);
    }
}

void
AstroFile::setComment(const QString& comment)
{
    if (this->comment != comment) {
        this->comment = comment;
        change(Comment);
    }
}

void
AstroFile::setHouseSystem(A::HouseSystemId system)
{
    if (getHouseSystem() != system) {
        scope.inputData.setHouseSystem(system);
        change(HouseSystem);
    }
}

void
AstroFile::setZodiac(A::ZodiacId zod)
{
    if (getZodiac() != zod) {
        scope.inputData.setZodiac(zod);
        change(Zodiac);
    }
}

void
AstroFile::setBaseChart(const QDateTime& baseGmt)
{
    if (!scope.inputData.hasBaseChart() || 
        scope.inputData.baseGMT() != baseGmt) {
        scope.inputData.setBaseChart(baseGmt);
        recalculate();
        change(ChangedState);
    }
}

void
AstroFile::clearBaseChart()
{
    if (scope.inputData.hasBaseChart()) {
        scope.inputData.clearBaseChart();
        recalculate();
        change(ChangedState);
    }
}

void
AstroFile::setTimezoneLocked(bool locked)
{
    if (_timezoneLocked == locked) return;
    _timezoneLocked = locked;
    change(ChangedState);
}

QString
AstroFile::getBaseName() const
{
    QString fullName = getName();
    // Extract base name from "Name in City" format
    int locIndex = fullName.indexOf(" in ");
    if (locIndex > 0) {
        return fullName.left(locIndex);
    }
    return fullName;
}

QString
AstroFile::getDisplayName() const
{
    QString base = getBaseName();
    if (_timezoneLocked && !locationName.isEmpty()) {
        // Extract just the city name (first part before comma)
        QString cityName = locationName.split(',').first().trimmed();
        return base + " in " + cityName;
    }
    return base;
}

void
AstroFile::setAspectSet(A::AspectSetId set, bool force)
{
    if (getAspectSet().id != set || force) {
        scope.inputData.setAspectSet(set);
        change(AspectSet);
    }
}

void
AstroFile::setAspectMode(const A::aspectModeType& mode)
{
    if (getAspectMode() != mode) {
        A::aspectMode = mode;
        change(AspectMode);
    }
}

AstroFile::Members
AstroFile::stampDisplaySettings()
{
    auto& ds = DisplaySettings::instance();
    Members flags = None;

    if (getHouseSystem() != ds.houseSystem()) {
        scope.inputData.setHouseSystem(ds.houseSystem());
        flags |= HouseSystem;
    }
    if (getZodiac() != ds.zodiac()) {
        scope.inputData.setZodiac(ds.zodiac());
        flags |= Zodiac;
    }
    if (getAspectSetId() != ds.aspectSet()) {
        scope.inputData.setAspectSet(ds.aspectSet());
        flags |= AspectSet;
    }
    // AspectMode is already global (A::aspectMode), no per-file copy needed
    // but we track the flag so handlers can react
    // (the actual value was already set by DisplaySettings::setAspectMode)

    if (flags & (HouseSystem | Zodiac))
        recalculate();              // full recalc for calculation-affecting settings
    else if (flags & AspectSet)
        recalculateBaseChart();     // cheaper path for aspect-only changes

    return flags;
}

void
AstroFile::setHarmonic(double harmonic)
{
    if (getHarmonic() != harmonic) {
        scope.harmonic = harmonic;
        change(Harmonic, false);   // transient display property — never mark dirty
    }
}

void
AstroFile::setEventList(const QList<QDateTime>& evl)
{
    if (getEventList() != evl) {
        _eventList = evl;
        change(ChangedState);
    }
}

QAbstractItemModel*
AstroFile::eventsModel()
{
    return _evm;
}

void
AstroFile::setEventsModel(QAbstractItemModel* model)
{
    if (_evm && _evm != model) {
        _evm->deleteLater();
    }
    _evm = model;
    if (_evm) {
        _evm->setParent(this);
    }
}

void
AstroFile::clearEventsModel()
{
    if (_evm) {
        _evm->deleteLater();
        _evm = nullptr;
    }
    _eventsNeedRecalc = true;
}

void
AstroFile::recalculate()
{
    qDebug() << "[PERF] Calculating file" << getName() << "type=" << type << "isProgressed=" << scope.inputData.isProgressed();
    clearPSSRContext(); // Clear cached PSSR context when chart is recalculated
    double h = scope.harmonic;          // preserve harmonic across full recalc
    scope = A::calculateAll(scope.inputData);
    if (h != 1.0) {
        scope.harmonic = h;
        A::calculateBaseChartHarmonic(scope);   // re-apply harmonic positions
    }
    qDebug() << "[PERF] Calculation complete for" << getName();
}

void
AstroFile::recalculateBaseChart()
{
    qDebug() << "Calculating base harmonic chart for file" << getName()
             << "...";
    A::calculateBaseChartHarmonic(scope);
}

void
AstroFile::recalculateHarmonics()
{
    qDebug() << "Calculating harmonics for file" << getName() << "...";
    // A::findHarmonics(scope);
}

void
AstroFile::destroy()
{
    if (getName().section(" ", -1).toInt() == counter) // latest file
        --counter;                                     // decrement file counter

    qDebug() << "Deleted file" << getName();
    // deleteLater();
    emit destroyRequested();
}

/*AstroFile :: ~AstroFile()
 {
  if (getName().section(" ", -1).toInt() == counter)   // latest file
    --counter;                                         // decrement file counter

  qDebug() << "Deleted file" << getName();
 }*/

/* =========================== ABSTRACT FILE HANDLER
 * ================================ */

AstroFileHandler::AstroFileHandler(QWidget* parent) :
    QWidget(parent),
    Customizable()
{
    delayUpdate = false;
    connect(&DisplaySettings::instance(),
            SIGNAL(changed(int)),
            this,
            SLOT(displaySettingsSlot(int)));
}

void
AstroFileHandler::setFiles(const AstroFileList& files)
{
    MembersList flags;
    int         i = 0;

    for (AstroFile* file : files) {
        AstroFile* old = (f.count() >= i + 1) ? f[i] : nullptr;
        if (file == old) {
            // Same file pointer - assume it may have been modified in place
            // Use All flags to trigger a full refresh
            flags << AstroFile::All;
        } else {
            if (old) {
                old->disconnect(this,
                                SLOT(fileUpdatedSlot(AstroFile::Members)));
                old->disconnect(this, SLOT(fileDestroyedSlot()));
            }

            if (file) {
                connect(file,
                        SIGNAL(changed(AstroFile::Members)),
                        this,
                        SLOT(fileUpdatedSlot(AstroFile::Members)));
                connect(file,
                        SIGNAL(destroyRequested()),
                        this,
                        SLOT(fileDestroyedSlot()));
                flags << file->diff(old);
            } else {
                flags.clear();
            }
        }

        i++;
    }

    f = files;

    // Split the accumulated diff flags into data vs view
    MembersList dataFlags, viewFlags;
    for (const auto& m : flags) {
        dataFlags << (m & ~AstroFile::ViewSettings);
        viewFlags << (m & AstroFile::ViewSettings);
    }

    if (isVisible() && !isAnyFileSuspended()) {
        delayMembers     = blankMembers();
        delayViewMembers = blankMembers();
        dispatchUpdate(dataFlags, viewFlags);
    } else {
        delayMembers     = dataFlags;
        delayViewMembers = viewFlags;
        delayUpdate      = true;
    }
}

A::AspectList
AstroFileHandler::calculateAspects()
{
    auto&       scope = file(0)->horoscope();
    const auto& input = scope.inputData;

    A::setOrbFactor(1);
    auto fp       = file(0)->focalPlanets();
    bool useFocal = !fp.empty();
    for (const auto& p : fp) {
        if (files().count() <= p.fileId()) {
            useFocal = false;
            break;
        }
    }
    if (!useFocal) {
        _syntheticMidpointPlanets.clear();
        _focalMidpoints.clear();
        scope.aspects = A::calculateAspects(A::getAspectSet(input.aspectSet()),
                                            scope.planets);
        return scope.aspects;
    }

    A::AspectSetId aspset = -1;
    const auto&    curr(A::EventOptions::current());
    if (fp.size() < curr.patternsQuorum) {
        bool        skip = fp.containsAny(A::Ingresses_Start, A::Ingresses_End);
        A::uintSSet hs   = A::dynAspState();

        auto hpc = A::findClusters(hs,
                                   { &file(0)->horoscope().planetsOrig },
                                   qMax(size_t(2), fp.size()),
                                   skip ? A::PlanetSet() : fp,
                                   false,
                                   false /*curr.patternsRestrictMoon*/,
                                   curr.expandShowOrb);

        for (const auto& h_pc : hpc) {
            const auto& pc = h_pc.second;
            for (const auto& p_c : pc) {
                const auto& pl = p_c.first;
                qDebug() << QString("H%1 %2 %3")
                                .arg(h_pc.first)
                                .arg(p_c.second)
                                .arg(pl.names().join('='));
                fp.insert(pl.begin(), pl.end());
            }
        }
        A::setOrbFactor(curr.expandShowOrb / A::harmonicsMaxQOrb());
    } else {
        aspset = MainWindow::theAstroWidget()->overrideAspectSet();
    }

    const auto& asps =
        A::getAspectSet(aspset == -1 ? input.aspectSet() : aspset);
    A::ChartPlanetPtrMap planets;
    // A::setOrbFactor(curr.patternsSpreadOrb / A::harmonicsMaxQOrb());
    _syntheticMidpointPlanets.clear();
    _focalMidpoints.clear();
    for (const auto& cpid : fp) {
        auto fid = cpid.fileId();
        if (fid < 0) continue;
        if (cpid.isMidpt()) {
            // Create a synthetic Planet at the midpoint of the two constituents
            auto p1 = file(fid)->horoscope().getPlanet(cpid.planetId());
            auto p2 = file(fid)->horoscope().getPlanet(cpid.planetId2());
            if (p1 && p2) {
                A::Planet synth;
                synth.id = A::PlanetId(-100 - _syntheticMidpointPlanets.size());
                synth.name = p1->name + "/" + p2->name;
                synth.isReal = false;
                // Compute near midpoint in ecliptic
                double diff = swe_difdeg2n(p2->eclipticPos.x(), p1->eclipticPos.x());
                synth.eclipticPos.setX(swe_degnorm(p1->eclipticPos.x() + diff / 2.0));
                synth.eclipticPos.setY((p1->eclipticPos.y() + p2->eclipticPos.y()) / 2.0);
                // Compute near midpoint in equatorial
                double raDiff = swe_difdeg2n(p2->equatorialPos.x(), p1->equatorialPos.x());
                synth.equatorialPos.setX(swe_degnorm(p1->equatorialPos.x() + raDiff / 2.0));
                synth.equatorialPos.setY((p1->equatorialPos.y() + p2->equatorialPos.y()) / 2.0);
                // Compute near midpoint in prime vertical
                double pvDiff = swe_difdeg2n(p2->pvPos, p1->pvPos);
                synth.pvPos = swe_degnorm(p1->pvPos + pvDiff / 2.0);
                _syntheticMidpointPlanets.append(synth);
                planets.emplace(cpid, &_syntheticMidpointPlanets.last());
                _focalMidpoints.append(cpid);
            }
        } else {
            auto pp = file(fid)->horoscope().getPlanet(cpid.planetId());
            planets.emplace(cpid, pp);
        }
    }
    auto alist = A::calculateAspects(asps, planets);
    A::setOrbFactor(1);
    return alist;
}

A::AspectList
AstroFileHandler::calculateSynastryAspects()
{
    qDebug() << "Calculate synatry apects" << file(0)->getAspectSet().id;
    auto useFocal = !file(1)->focalPlanets().empty();
    for (const auto& p : file(1)->focalPlanets()) {
        if (files().count() <= p.fileId()) {
            useFocal = false;
            break;
        }
    }
    if (!useFocal) {
        _syntheticMidpointPlanets.clear();
        _focalMidpoints.clear();
        A::setOrbFactor(0.25);
        return A::calculateAspects(file(0)->getAspectSet(),
                                   file(0)->horoscope().planets,
                                   file(1)->horoscope().planets);
    }

    // is alt being held? @todo this should probably just be the default behavior
    bool alt = (QApplication::keyboardModifiers() & Qt::AltModifier);

    A::AspectSetId aspset = -1;

    auto fp = file(1)->focalPlanets();
    if (fp.empty()) fp = file(1)->focalPlanets();

    const auto& curr(A::EventOptions::current());
    if (fp.size() < curr.patternsQuorum) {
        bool skip = fp.containsAny(A::Ingresses_Start, A::Ingresses_End)
                    || (fp.size() == 2
                        && fp.begin()->planetId() == fp.rbegin()->planetId());
        A::uintSSet hs;
#if 0
        uint h;
        bool ok;
        if (false
                && asps.name.startsWith("H")
                && ((h = asps.name.midRef(1).toUInt(&ok)), ok))
        {
            hs.insert(h);
        } else
#endif
        hs = A::dynAspState();

        A::PlanetProfile pf { &file(0)->horoscope().planetsOrig,
                              &file(1)->horoscope().planetsOrig };
        A::setOrbFactor(curr.expandShowOrb / A::harmonicsMaxQOrb());
        QList<A::Aspect> alist;
        _syntheticMidpointPlanets.clear();
        _focalMidpoints.clear();
        auto             hpc = A::findClusters(hs,
                                   pf,
                                   qMax(size_t(2), fp.size()),
                                   /*skip &&*/ alt ? A::PlanetSet() : fp,
                                   true /*skipAllNatalOnly*/,
                                   false /*curr.patternsRestrictMoon*/,
                                   curr.expandShowOrb);
        for (const auto& h_pc : hpc) {
            auto         h  = h_pc.first;
            const auto&  pc = h_pc.second;
            A::PlanetSet ps;
            for (const auto& p_c : pc) {
                const auto& pl = p_c.first;
                qDebug() << QString("H%1 %2 %3")
                                .arg(h_pc.first)
                                .arg(p_c.second)
                                .arg(pl.names().join('='));
                ps.insert(pl.begin(), pl.end());
            }
            A::ChartPlanetPtrMap planets;
            for (const auto& cpid : ps) {
                auto fid = cpid.fileId();
                if (fid < 0) continue;
                if (cpid.isMidpt()) {
                    auto p1 = file(fid)->horoscope().getPlanet(cpid.planetId());
                    auto p2 = file(fid)->horoscope().getPlanet(cpid.planetId2());
                    if (p1 && p2) {
                        A::Planet synth;
                        synth.id = A::PlanetId(-100 - _syntheticMidpointPlanets.size());
                        synth.name = p1->name + "/" + p2->name;
                        synth.isReal = false;
                        double diff = swe_difdeg2n(p2->eclipticPos.x(), p1->eclipticPos.x());
                        synth.eclipticPos.setX(swe_degnorm(p1->eclipticPos.x() + diff / 2.0));
                        synth.eclipticPos.setY((p1->eclipticPos.y() + p2->eclipticPos.y()) / 2.0);
                        double raDiff = swe_difdeg2n(p2->equatorialPos.x(), p1->equatorialPos.x());
                        synth.equatorialPos.setX(swe_degnorm(p1->equatorialPos.x() + raDiff / 2.0));
                        synth.equatorialPos.setY((p1->equatorialPos.y() + p2->equatorialPos.y()) / 2.0);
                        double pvDiff = swe_difdeg2n(p2->pvPos, p1->pvPos);
                        synth.pvPos = swe_degnorm(p1->pvPos + pvDiff / 2.0);
                        _syntheticMidpointPlanets.append(synth);
                        planets.emplace(cpid, &_syntheticMidpointPlanets.last());
                        _focalMidpoints.append(cpid);
                    }
                } else {
                    auto pp = file(fid)->horoscope().getPlanet(cpid.planetId());
                    planets.emplace(cpid, pp);
                }
            }
            const auto& asps = A::getAspectSet(A::topAspectSet().id + h);
            alist << A::calculateAspects(asps, planets);
        }

        // findClusters doesn't know about midpoint ChartPlanetIds, so any
        // midpoint cpids in the original focal set won't appear in the
        // cluster results.  Walk the original fp and ensure every midpoint
        // is represented in _focalMidpoints (needed by drawMidpointFigures).
        for (const auto& cpid : fp) {
            if (!cpid.isMidpt()) continue;
            // Already added from a cluster result?
            if (_focalMidpoints.contains(cpid)) continue;
            auto fid = cpid.fileId();
            if (fid < 0 || fid >= filesCount()) continue;
            auto p1 = file(fid)->horoscope().getPlanet(cpid.planetId());
            auto p2 = file(fid)->horoscope().getPlanet(cpid.planetId2());
            if (p1 && p2) {
                A::Planet synth;
                synth.id = A::PlanetId(-100 - _syntheticMidpointPlanets.size());
                synth.name = p1->name + "/" + p2->name;
                synth.isReal = false;
                double diff = swe_difdeg2n(p2->eclipticPos.x(), p1->eclipticPos.x());
                synth.eclipticPos.setX(swe_degnorm(p1->eclipticPos.x() + diff / 2.0));
                synth.eclipticPos.setY((p1->eclipticPos.y() + p2->eclipticPos.y()) / 2.0);
                double raDiff = swe_difdeg2n(p2->equatorialPos.x(), p1->equatorialPos.x());
                synth.equatorialPos.setX(swe_degnorm(p1->equatorialPos.x() + raDiff / 2.0));
                synth.equatorialPos.setY((p1->equatorialPos.y() + p2->equatorialPos.y()) / 2.0);
                double pvDiff = swe_difdeg2n(p2->pvPos, p1->pvPos);
                synth.pvPos = swe_degnorm(p1->pvPos + pvDiff / 2.0);
                _syntheticMidpointPlanets.append(synth);
                _focalMidpoints.append(cpid);
            }
        }

        A::setOrbFactor(1);
        return alist;
    } else {
        aspset = MainWindow::theAstroWidget()->overrideAspectSet();
        A::setOrbFactor(1);
    }

    const auto& asps = A::getAspectSet(
        aspset == -1 ? file(0)->horoscope().inputData.aspectSet() : aspset);
    // const auto& asps = aspset != -1? A::getAspectSet(aspset) :
    // file(0)->getAspectSet();
    A::ChartPlanetPtrMap planets;
    _syntheticMidpointPlanets.clear();
    _focalMidpoints.clear();
    for (const auto& cpid : fp) {
        auto fid = cpid.fileId();
        if (fid < 0) continue;
        if (cpid.isMidpt()) {
            auto p1 = file(fid)->horoscope().getPlanet(cpid.planetId());
            auto p2 = file(fid)->horoscope().getPlanet(cpid.planetId2());
            if (p1 && p2) {
                A::Planet synth;
                synth.id = A::PlanetId(-100 - _syntheticMidpointPlanets.size());
                synth.name = p1->name + "/" + p2->name;
                synth.isReal = false;
                double diff = swe_difdeg2n(p2->eclipticPos.x(), p1->eclipticPos.x());
                synth.eclipticPos.setX(swe_degnorm(p1->eclipticPos.x() + diff / 2.0));
                synth.eclipticPos.setY((p1->eclipticPos.y() + p2->eclipticPos.y()) / 2.0);
                double raDiff = swe_difdeg2n(p2->equatorialPos.x(), p1->equatorialPos.x());
                synth.equatorialPos.setX(swe_degnorm(p1->equatorialPos.x() + raDiff / 2.0));
                synth.equatorialPos.setY((p1->equatorialPos.y() + p2->equatorialPos.y()) / 2.0);
                double pvDiff = swe_difdeg2n(p2->pvPos, p1->pvPos);
                synth.pvPos = swe_degnorm(p1->pvPos + pvDiff / 2.0);
                _syntheticMidpointPlanets.append(synth);
                planets.emplace(cpid, &_syntheticMidpointPlanets.last());
                _focalMidpoints.append(cpid);
            }
        } else {
            auto pp = file(fid)->horoscope().getPlanet(cpid.planetId());
            planets.emplace(cpid, pp);
        }
    }
    auto alist = A::calculateAspects(asps, planets);
    A::setOrbFactor(1);
    return alist;
}

MembersList
AstroFileHandler::blankMembers()
{
    MembersList ret;
    for (int i = 0; i < f.count(); i++) ret << AstroFile::Members();
    return ret;
}

bool
AstroFileHandler::isAnyFileSuspended()
{
    for (auto file : std::as_const(f))
        if (file->isSuspendedUpdate()) return true;
    return false;
}

void
AstroFileHandler::fileUpdatedSlot(AstroFile::Members m)
{
    int i = f.indexOf((AstroFile*) sender());
    if (i == -1) return; // file is not in set (yet?)

    // Split incoming flags into data-only and view-only sets
    AstroFile::Members dataFlags = m & ~AstroFile::ViewSettings;
    AstroFile::Members viewFlags = m & AstroFile::ViewSettings;

    if (isVisible() && !isAnyFileSuspended()) {
        MembersList dataList, viewList;
        if (delayUpdate) {
            dataList        = delayMembers;
            viewList        = delayViewMembers;
            delayMembers     = blankMembers();
            delayViewMembers = blankMembers();
            delayUpdate      = false;
        } else {
            dataList = blankMembers();
            viewList = blankMembers();
        }

        while (dataList.count() <= i)
            dataList.append(AstroFile::Members());
        while (viewList.count() <= i)
            viewList.append(AstroFile::Members());

        dataList[i] |= dataFlags;
        viewList[i] |= viewFlags;

        dispatchUpdate(dataList, viewList);
    } else {
        delayUpdate = true;
        while (delayMembers.count() <= i)
            delayMembers.append(AstroFile::Members());
        while (delayViewMembers.count() <= i)
            delayViewMembers.append(AstroFile::Members());
        delayMembers[i] |= dataFlags;
        delayViewMembers[i] |= viewFlags;
    }
}

void
AstroFileHandler::dispatchUpdate(const MembersList& dataFlags,
                                 const MembersList& viewFlags)
{
    // Always deliver data flags (includes ChangedState, Name, GMT, etc.)
    bool hasData = false;
    for (const auto& m : dataFlags)
        if (m) { hasData = true; break; }
    if (hasData)
        filesUpdated(dataFlags);

    // Deliver view-setting flags through the separate virtual
    bool hasView = false;
    for (const auto& m : viewFlags)
        if (m) { hasView = true; break; }
    if (hasView)
        viewSettingsUpdated(viewFlags);
}

void
AstroFileHandler::fileDestroyedSlot()
{
    int i = f.indexOf((AstroFile*) sender());
    if (i == -1)
        return; // ignore if destroying file not in list (e.g. in other tab)

    MembersList mList = blankMembers();
    if (i < f.count() - 1)
        mList[i] =
            f[i + 1]->diff(f[i]); // write difference with next file in list
    f.removeAt(i);
    mList.removeLast();

    // Split diff flags into data vs view
    MembersList dataFlags, viewFlags;
    for (const auto& m : mList) {
        dataFlags << (m & ~AstroFile::ViewSettings);
        viewFlags << (m & AstroFile::ViewSettings);
    }

    // Always notify handlers when a file is removed, even if the diff flags
    // for the remaining file(s) are empty.  The file *count* changed, so
    // handlers like Chart need to rebuild their scene (e.g. 2-chart → 1-chart).
    bool hasAny = false;
    for (const auto& m : dataFlags)
        if (m) { hasAny = true; break; }
    if (!hasAny)
        for (const auto& m : viewFlags)
            if (m) { hasAny = true; break; }

    if (!hasAny && !dataFlags.isEmpty()) {
        // Force at least a minimal data notification so filesUpdated() fires
        dataFlags[0] |= AstroFile::Name;
    }

    dispatchUpdate(dataFlags, viewFlags);
}

void
AstroFileHandler::displaySettingsSlot(int flags)
{
    if (!filesCount()) return;

    // Stamp the new settings into every file's InputData and recalculate
    for (AstroFile* af : f) {
        af->stampDisplaySettings();
    }

    // AspectMode change requires a full recalculation so that positions
    // computed at calc-time (e.g. fixed star pvPos) are up to date.
    if (flags & AstroFile::AspectMode) {
        for (AstroFile* af : f) {
            af->calculate();
        }
    }

    // Build per-file Members list from the DisplaySettings flags
    AstroFile::Members viewMembers = AstroFile::Members::fromInt(flags);
    MembersList viewFlags;
    for (int i = 0; i < filesCount(); ++i) {
        viewFlags << viewMembers;
    }

    if (!isVisible() || isAnyFileSuspended()) {
        // Accumulate for later delivery
        while (delayViewMembers.size() < filesCount())
            delayViewMembers << AstroFile::None;
        for (int i = 0; i < filesCount(); ++i)
            delayViewMembers[i] = delayViewMembers[i] | viewMembers;
        delayUpdate = true;
        return;
    }

    // Dispatch directly to viewSettingsUpdated() — no data flags
    MembersList noData;
    for (int i = 0; i < filesCount(); ++i)
        noData << AstroFile::None;
    dispatchUpdate(noData, viewFlags);
}

void
AstroFileHandler::resumeUpdate()
{
    if (delayUpdate) {
        MembersList data = delayMembers;
        MembersList view = delayViewMembers;
        delayMembers     = blankMembers();
        delayViewMembers = blankMembers();
        delayUpdate      = false;
        dispatchUpdate(data, view);
    }
}

/* =========================== ASTRO TREE VIEW
 * ====================================== */

/*AstroTreeView :: AstroTreeView (QWidget *parent) : QTreeWidget(parent)
 {
  file = 0;
  updateIfVisible = false;
  setHeaderHidden(true);
  setStatusTip(" ");                 // keep status bar clear
 }

QList<AstroTreeView::Topics> AstroTreeView :: getTopics()
 {
  QList<Topics> ret;

  ret << Topic_PersonalLife
      << Topic_MarriageAndChildren
      << Topic_Health
      << Topic_Financial;

  return ret;
 }

QStringList AstroTreeView :: getTopicNames()
 {
  QStringList ret;

  ret << tr("Personal life")
      << tr("Marriage and children")
      << tr("Health")
      << tr("Financial");

  return ret;
 }

void
AstroTreeView :: setTopic(Topics topic)
 {
  this->topic = topic;

  if (isVisible())
    updateItems();
  else
    updateIfVisible = true;
 }

void
AstroTreeView :: setFile(AstroFile* file)
 {
  this->file = file;

  if (isVisible())
    updateItems();
  else
    updateIfVisible = true;
 }

void
AstroTreeView :: updateItems()
 {
  clear();
  updateIfVisible = false;

  if (!file || file->isEmpty())
    return;

  switch (topic)
   {
    case Topic_PersonalLife:        addPersonalLifeItems(); break;
    case Topic_MarriageAndChildren: addMarriageItems();     break;
    case Topic_Health:              addHealthItems();       break;
    case Topic_Financial:           addFinancialItems();    break;
   }

  expandAll();
 }

void
AstroTreeView :: addTopLevelItem(const QString& text)
 {
  QTreeWidgetItem* item = new QTreeWidgetItem;
  item->setText(0, text);

  QFont font = this->font();
  font.setPointSize(font.pointSize() + 2);
  item->setFont(0, font);

  ((QTreeWidget*)this)->addTopLevelItem(item);
 }

void
AstroTreeView :: addChildItem(const QString& text, bool active)
 {
  QTreeWidgetItem* item = topLevelItem(topLevelItemCount() - 1);
  QTreeWidgetItem* child = new QTreeWidgetItem;

  child->setText(0, text);
  child->setFlags(Qt::NoItemFlags);
  child->setDisabled(!active);

  item->addChild(child);
 }

void
AstroTreeView :: addPersonalLifeItems()
 {
  if (file->getType() != AstroFile::TypeMale &&
      file->getType() != AstroFile::TypeFemale) return;

  const A::Horoscope& h = file->horoscope();

  addTopLevelItem(tr("Loving relationship with a large age difference"));

  addChildItem(tr("Venus is harmoniously aspected to Saturn"),
               A::aspect(h.venus, h.saturn) == A::Aspect_Conjunction ||
               A::aspect(h.venus, h.saturn) == A::Aspect_Trine ||
               A::aspect(h.venus, h.saturn) == A::Aspect_Sextile);
  addChildItem(tr("Saturn is harmoniously aspected and is disposited in V
house"), h.saturn.house == 5 && hasHarmonicAspects(h.saturn, h));


  addTopLevelItem("Love affair with a foreigner");

  addChildItem(tr("Ruler of IX house is located in V house, or vice versa"),
               A::rulerDisposition(5, 9, h) ||
               A::rulerDisposition(9, 5, h));
  addChildItem(tr("Venus is disposited in IX house"),
               h.venus.house == 9);


  addTopLevelItem(tr("Secret love affair"));

  addChildItem(tr("Venus is disposited in XII house"),
               h.venus.house == 12);
  addChildItem(tr("Ruler of XII house is located in V house, or vice versa"),
               A::rulerDisposition(5, 12, h) ||
               A::rulerDisposition(12, 5, h));
  addChildItem(tr("Ruler of V house is aspected to ruler of XII house"),
               A::aspect(A::ruler(5, h),
                                 A::ruler(12, h)) != A::Aspect_None);

  if (file->getType() == AstroFile::TypeMale)
   {
    addChildItem(tr("Moon has aspect with Uranus - love affair with married
woman"), A::aspect(h.moon, h.uranus) != A::Aspect_None); addChildItem(tr("Venus
has aspect with Uranus - free relationships"), A::aspect(h.venus, h.uranus) !=
A::Aspect_None);
   }

  addChildItem(tr("Venus is in quadrature or opposition with Neptune"),
               A::aspect(h.venus, h.neptune) == A::Aspect_Quadrature ||
               A::aspect(h.venus, h.neptune) == A::Aspect_Opposition);
 }

void
AstroTreeView :: addMarriageItems()
 {
  if (file->getType() != AstroFile::TypeMale &&
      file->getType() != AstroFile::TypeFemale) return;

  const A::Horoscope& h = file->horoscope();

  addTopLevelItem(tr("Early marriage"));
  A::Planet ms = getMarriageSignificator(file);

  addChildItem(tr("Marriage significator (%1) is wealth and
strong").arg(ms.name), !hasDamage(ms, h) && hasHarmonicAspects(ms, h));
  addChildItem(tr("Sun is disposited in V or VII house"),
               h.sun.house == 5 || h.sun.house == 7);
  addChildItem(tr("Moon is disposited in V or VII house"),
               h.moon.house == 5 || h.moon.house == 7);


  addTopLevelItem(tr("Celibacy"));

  addChildItem(tr("Saturn is disposited in VII house"),
               h.saturn.house == 7);

  addChildItem(tr("Marriage significator (%1) is damaged by
Saturn").arg(ms.name), A::aspect(ms, h.saturn) == A::Aspect_Quadrature ||
               A::aspect(ms, h.saturn) == A::Aspect_Opposition);

  addChildItem(tr("Ruler of VII house is damaged by Saturn"),
               A::aspect(A::ruler(7, h), h.saturn) == A::Aspect_Quadrature ||
               A::aspect(A::ruler(7, h), h.saturn) == A::Aspect_Opposition);

  addChildItem(tr("Uranus is disposited in VII house (excessive independence)"),
               h.uranus.house == 7);

  addChildItem(tr("Venus is located in major aspect with Neptune (excessive
idealism)"), A::aspect(h.venus, h.neptune) != A::Aspect_None);


  addTopLevelItem(tr("Plural marriage"));

  addChildItem(tr("Asc-Dsc axis lays in mutable cross"),
               A::getSignNumber(h.houses.cusp[0]) % 3 == 0); // mutable signs
are: 3, 6, 9, 12

  addChildItem(tr("Uranus or Pluto is disposited in VII house"),
               h.uranus.house == 7 || h.pluto.house == 7);

  addChildItem(tr("Marriage significator (%1) is located in Gemini, Saggitarius
or Pisces") .arg(ms.name), ms.sign == 3 || ms.sign == 9 || ms.sign == 12);

  addChildItem(tr("Jupiter is disposited in VII house and is located in "
                  "Gemini, Saggitarius, Aquarius or Pisces"),
               h.jupiter.house == 7 && (h.jupiter.sign == 3  ||
                                        h.jupiter.sign == 9  ||
                                        h.jupiter.sign == 11 ||
                                        h.jupiter.sign == 12));

  addChildItem(tr("Ruler of VII house is located in major aspect with Uranus,
Mercury or Moon"), A::aspect(A::ruler(7, h), h.uranus)  != A::Aspect_None ||
               A::aspect(A::ruler(7, h), h.mercury) != A::Aspect_None ||
               A::aspect(A::ruler(7, h), h.moon)    != A::Aspect_None);


  addTopLevelItem(tr("Widowhood"));

  if (file->getType() == AstroFile::TypeFemale)
    addChildItem(tr("Sun is located in tense aspect with Saturn"),
                 A::aspect(h.sun, h.saturn) == A::Aspect_Quadrature ||
                 A::aspect(h.sun, h.saturn) == A::Aspect_Opposition);
  if (file->getType() == AstroFile::TypeMale)
    addChildItem(tr("Moon is located in tense aspect with Saturn"),
                 A::aspect(h.moon, h.saturn) == A::Aspect_Quadrature ||
                 A::aspect(h.moon, h.saturn) == A::Aspect_Opposition);
  addChildItem(tr("Saturn or Pluto is disposited in VII house"),
               h.saturn.house == 7 || h.pluto.house == 7);
  addChildItem(tr("Ruler of VII house is located in VIII house, or vice versa"),
               A::rulerDisposition(7, 8, h) ||
               A::rulerDisposition(8, 7, h));


  addTopLevelItem(tr("Late marriage (marriage with a large age difference)"));

  addChildItem(tr("Saturn is wealth and is disposited in VII house"),
               h.saturn.house == 7 && hasHarmonicAspects(h.saturn, h));

  addChildItem(tr("Ruler of VII house is harmoniously aspected to Saturn"),
               A::aspect(A::ruler(7,h), h.saturn) == A::Aspect_Conjunction ||
               A::aspect(A::ruler(7,h), h.saturn) == A::Aspect_Trine ||
               A::aspect(A::ruler(7,h), h.saturn) == A::Aspect_Sextile);

  addChildItem(tr("Venus is harmoniously aspected to Saturn"),
               A::aspect(h.venus, h.saturn) == A::Aspect_Conjunction ||
               A::aspect(h.venus, h.saturn) == A::Aspect_Trine ||
               A::aspect(h.venus, h.saturn) == A::Aspect_Sextile);


  addTopLevelItem(tr("Civil marriage"));

  addChildItem(tr("Uranus is disposited in VII house"),
               h.uranus.house == 7);

  addChildItem(tr("Ruler of VII house is located in major aspect with Uranus"),
               A::aspect(A::ruler(7,h), h.uranus) != A::Aspect_None);


  addTopLevelItem(tr("Marriage to a foreigner"));

  addChildItem(tr("Ruler of IX house is located in VII house, or vice versa"),
               A::rulerDisposition(7, 9, h) ||
               A::rulerDisposition(9, 7, h));

  addChildItem(tr("Venus is disposited in IX house"),
               h.venus.house == 9);


  addTopLevelItem(tr("False marriage (or marriage of convenience)"));

  addChildItem(tr("Ruler of VII house is disposited in II house"),
               A::ruler(7, h).house == 2);

  addChildItem(tr("Ruler of VII house is located in major aspect with ruler of
II house"), A::aspect(A::ruler(7,h), A::ruler(2,h)) != A::Aspect_None);

  addChildItem(tr("Ruler of VII house is disposited in IV house"),
               A::ruler(7, h).house == 4);

  addChildItem(tr("Ruler of VII house is located in major aspect with ruler of
IV house"), A::aspect(A::ruler(7,h), A::ruler(4,h)) != A::Aspect_None);

  addChildItem(tr("Ruler of VII house is disposited in X house"),
               A::ruler(7, h).house == 10);

  addChildItem(tr("Marriage significator (%1) is located in tense aspect with
Neptune") .arg(ms.name), A::aspect(ms, h.neptune) == A::Aspect_Quadrature ||
               A::aspect(ms, h.neptune) == A::Aspect_Opposition);

  addChildItem(tr("Sun or Moon is located in tense aspect with Neptune"),
               A::aspect(h.sun,  h.neptune) == A::Aspect_Quadrature ||
               A::aspect(h.sun,  h.neptune) == A::Aspect_Opposition ||
               A::aspect(h.moon, h.neptune) == A::Aspect_Quadrature ||
               A::aspect(h.moon, h.neptune) == A::Aspect_Opposition);

  addChildItem(tr("Ruler of VII house is located in major aspect with ruler of X
house"), A::aspect(A::ruler(7, h), A::ruler(10,h)) != A::Aspect_None);


  addTopLevelItem(tr("Childlessness"));

  addChildItem(tr("Saturn is disposited in V house"),
               h.saturn.house == 5);

  addChildItem(tr("Ruler of V house is located in tense aspect with Saturn"),
               A::aspect(A::ruler(5, h), h.saturn) == A::Aspect_Quadrature ||
               A::aspect(A::ruler(5, h), h.saturn) == A::Aspect_Opposition);

  addChildItem(tr("Moon is disposited in V house and is located in tense aspect
with Saturn"), h.moon.house == 5 && (A::aspect(h.moon, h.saturn) ==
A::Aspect_Quadrature || A::aspect(h.moon, h.saturn) == A::Aspect_Opposition));



  addTopLevelItem(tr("Fertility and large family"));

  int count = 0;
  foreach (const A::Planet& p, h.planets)
    if (p.id != A::Planet_Saturn &&
        p.house == 5 &&
        !hasDamage(p,h)) count++;

  addChildItem(tr("A lot of wealth planets are disposited in V house"),
               count >= 2);

  addChildItem(tr("A huge size of V house (>45°)"),
               h.houses.cusp[5] - h.houses.cusp[4] > 45);

  addChildItem(tr("Jupiter is disposited in V house"),
               h.jupiter.house == 5);

  if (file->getType() == AstroFile::TypeMale)
    addChildItem(tr("Moon is disposited in V house"),
                 h.moon.house == 5);

  if (file->getType() == AstroFile::TypeFemale)
    addChildItem(tr("Sun is located in Taurus (or water sign) and is disposited
in V house"), h.sun.house == 5 && (h.sun.sign == 2 || h.sun.sign == 4 ||
                                      h.sun.sign == 8 ||
                                      h.sun.sign == 12));



  if (file->getType() == AstroFile::TypeFemale)
   {
    addTopLevelItem(tr("Aborting of pregnancy"));

    A::AspectId aspect1 = A::aspect(A::ruler(5,h), h.mars);
    A::AspectId aspect2 = A::aspect(A::ruler(5,h), h.saturn);
    A::AspectId aspect3 = A::aspect(A::ruler(5,h), h.uranus);
    A::AspectId aspect4 = A::aspect(A::ruler(5,h), h.pluto);
    addChildItem(tr("Ruler of V house is damaged by 'wicked' planet"),
                 aspect1 == A::Aspect_Opposition ||
                 aspect2 == A::Aspect_Opposition ||
                 aspect3 == A::Aspect_Opposition ||
                 aspect4 == A::Aspect_Opposition ||
                 aspect1 == A::Aspect_Quadrature ||
                 aspect2 == A::Aspect_Quadrature ||
                 aspect3 == A::Aspect_Quadrature ||
                 aspect4 == A::Aspect_Quadrature);

    addChildItem(tr("Mars, Uranus or Pluto is damaged by other planet and is
disposited in V house"), (h.mars.house   == 5 && hasDamage(h.mars,   h)) ||
                 (h.uranus.house == 5 && hasDamage(h.uranus, h)) ||
                 (h.pluto.house  == 5 && hasDamage(h.pluto,  h)));
   }



  addTopLevelItem(tr("Children in foster care"));

  addChildItem(tr("Ruler of V house is wealth and is diposited in III house, or
vice-versa"), (A::rulerDisposition(5, 3, h) && !hasDamage(A::ruler(3,h), h)) ||
               (A::rulerDisposition(3, 5, h) && !hasDamage(A::ruler(5,h), h)));




  addTopLevelItem(tr("Children out of wedlock"));

  addChildItem(tr("Neptune is wealth and is disposited in V house"),
               h.neptune.house == 5 && !hasDamage(h.neptune, h));

  addChildItem(tr("Ruler of V house is wealth and is diposited in XII house, or
vice-versa"), (A::rulerDisposition(5, 12, h) && !hasDamage(A::ruler(12,h), h))
|| (A::rulerDisposition(12, 5, h) && !hasDamage(A::ruler(5,h), h)));



  addTopLevelItem(tr("Separation from child"));

  addChildItem(tr("Neptune is damaged and is disposited in V house"),
               h.neptune.house == 5 && hasDamage(h.neptune, h));

  addChildItem(tr("Ruler of V house is damaged and is diposited in XII house, or
vice-versa"), (A::rulerDisposition(5, 12, h) && hasDamage(A::ruler(12,h), h)) ||
               (A::rulerDisposition(12, 5, h) && hasDamage(A::ruler(5,h), h)));



  addTopLevelItem(tr("Death of a child"));

  addChildItem(tr("Saturn or Pluto is disposited in V house"),
               h.saturn.house == 5 || h.pluto.house == 5);

  bool b = false;
  foreach (const A::Planet& p, h.planets)
    if (p.house == 8 && (A::aspect(p, A::ruler(5,h)) == A::Aspect_Opposition ||
                            A::aspect(p, A::ruler(5,h)) ==
A::Aspect_Quadrature)) b = true;

  addChildItem(tr("Ruler of V house is in quadrature or opposition to element of
VIII house"), b);

  addChildItem(tr("Ruler of V house is diposited in VIII house, or vice-versa"),
               A::rulerDisposition(5, 8, h) ||
               A::rulerDisposition(8, 5, h));
 }

void
AstroTreeView :: addHealthItems()
 {
  //const A::Horoscope& h = file->horoscope();

  addTopLevelItem("sdfsdfsdf");
 }

void
AstroTreeView :: addFinancialItems()
 {
  //const A::Horoscope& h = file->horoscope();

  addTopLevelItem("asfdg");
 }

const A::Planet& AstroTreeView :: getMarriageSignificator ( AstroFile* file )
 {
  bool sunAbove = file->horoscope().sun.horizontalPos.y() > 0;  // sun is above
the horizon

  if (file->getType() == AstroFile::TypeMale)
   {
    if (sunAbove)
      return file->horoscope().venus;
    else
      return file->horoscope().moon;
   }
  else if (file->getType() == AstroFile::TypeFemale)
   {
    if (sunAbove)
      return file->horoscope().sun;
    else
      return file->horoscope().mars;
   }

  return A::Planet();
 }

bool AstroTreeView :: hasDamage (const A::Planet& planet, const A::Horoscope
&scope)
 {
  foreach (const A::Planet& p, scope.planets)
   {
    A::AspectId aspect = A::aspect(planet, p);
    if (aspect == A::Aspect_Opposition ||
        aspect == A::Aspect_Quadrature)
      return true;
   }

  return false;
 }

bool AstroTreeView :: hasHarmonicAspects (const A::Planet& planet, const
A::Horoscope &scope)
 {
  foreach (const A::Planet& p, scope.planets)
   {
    A::AspectId aspect = A::aspect(planet, p);
    if (aspect == A::Aspect_Trine ||
        aspect == A::Aspect_Sextile)
      return true;
   }

  return false;
 }
*/

/* =========================== ASTRO TOPICS SHOW
 * ==================================== */

/*AstrotTopicsShow :: AstrotTopicsShow(QWidget *parent) :
AstroFileHandler(parent)
 {
  QLabel* label1 = new QLabel(tr("Natal horoscope analysis"));
  tabs = new QTabWidget;

  label1 -> setStatusTip(tr("topicsScreen"));
  label1 -> setAlignment(Qt::AlignCenter);


  QStringList names = AstroTreeView::getTopicNames();
  for (int i = 0; i < names.count(); i++)
   {
    AstroTreeView* tree = new AstroTreeView;
    tree->setTopic(AstroTreeView::getTopics()[i]);
    tabs->addTab(tree, names[i]);
   }


  QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(label1);
    layout->addWidget(tabs);
 }

void
AstrotTopicsShow :: resetToDefault()
 {
  fileUpdated(AstroFile::All);
 }

void
AstrotTopicsShow :: fileUpdated(AstroFile::Members)
 {
  for (int i = 0; i < tabs->count(); i++)
   {
    AstroTreeView* tree = (AstroTreeView*)tabs->widget(i);
    tree->setFile(file());
    //if (tree->isEmpty()) tabs->removeTab(i);

    // TODO: hide empty tabs
   }
 }
*/
