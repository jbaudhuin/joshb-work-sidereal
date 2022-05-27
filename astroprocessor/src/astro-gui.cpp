#include <memory>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTextCodec>
#include <QDebug>
#include <QStandardPaths>
#include <QMetaType>

#include "astro-calc.h"
#include "astro-gui.h"
#include <Astroprocessor/Zodiac>

/* ====================== ASTRO FILE ============================= */

/*static*/ int AstroFile::counter = 0;

AstroFile::AstroFile(QObject* parent) : QObject(parent)
{
    do {
        setName(tr("Untitled %1").arg(++counter));
    } while (fileInfo().exists());

    type = TypeOther;
    unsavedChanges = false;
    holdUpdate = false;
    holdUpdateMembers = None;
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
    case TypeSearch: return "Search";
    case TypeDerivedSA: return "SA";
    case TypeDerivedProg: return "Prog";
    case TypeDerivedPD: return "PD";
    case TypeDerivedSearch: return "Der";
    case TypeMale: return "Male";
    case TypeFemale: return "Female";
    case TypeOther: return "Other";
    default: break;
    }
    return "";
}

FileType
AstroFile::typeFromString(const QString& str)
{
    if (str == "Male")   return TypeMale;
    if (str == "Female") return TypeFemale;
    if (str == "Der") return TypeDerivedSearch;
    if (str == "PD") return TypeDerivedPD;
    if (str == "Prog") return TypeDerivedProg;
    if (str == "SA") return TypeDerivedSA;
    if (str == "Search") return TypeSearch;
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
    if (getHouseSystem() != other->getHouseSystem()) flags |= HouseSystem;
    if (getZodiac() != other->getZodiac()) flags |= Zodiac;
    if (getAspectSet().id != other->getAspectSet().id) flags |= AspectSet;
    if (getEventList() != other->getEventList()) flags |= EventList;
    if (hasUnsavedChanges() != other->hasUnsavedChanges())
        flags |= ChangedState;
    //lastChangedMembers = flags;
    return flags;
}

/*static*/
QMap<QString,QString> &
AstroFile::_fixedChartDirMap()
{
    static QMap<QString,QString> s_map;
    if (s_map.empty()) {
        constexpr auto loc = QStandardPaths::DocumentsLocation;
        QString dir = QStandardPaths::writableLocation(loc)
                      + "/zodiac-charts";

        QDir d(dir);
        if (!d.exists()) QDir().mkpath(d.absolutePath());

        s_map["User Charts"] = dir;
        s_map["Sample Charts"] = "user/";
        _fixedChartDirMapKeys() << "User Charts" << "Sample Charts";
    }
    return s_map;
}

/*static*/
QStringList &
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
        _fileInfo.setFile(QDir(fixedChartDir()),
                          _fileInfo.fileName());
    }
    QSettings file(fileName(), QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
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
    file.setValue("comment", getComment());

    //if (getType()==TypeEvents) {
    file.setValue("dateRange", getDateRange().operator QVariant());
    if (_eventList.empty()) {
        file.setValue("eventList", QVariant());
    } else {
        QVariantList vl;
        for (const auto& dt: qAsConst(_eventList)) {
            vl << dt;
        }
        file.setValue("eventList", vl);
    }
    //}

    qDebug() << "Saved" << getName() << "to" << fileName();

    clearUnsavedState();
}

void
AstroFile::load(const AFileInfo& fi/*, bool recalculate*/)
{
    QString name = fi.baseName();
    if (name.isEmpty()) return;
    qDebug() << "Overwriting" << getName() << "from" << fi.absoluteFilePath();

    suspendUpdate();
    setFocalPlanets();
    _fileInfo = fi;

    QSettings file(fileName(), QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    file.setIniCodec(QTextCodec::codecForName("UTF-8"));
#endif

    for (const QString& val : file.allKeys()) {
        qDebug() << val << file.value(val);
    }

    setType(typeFromString(file.value("type").toString()));

    auto dts = file.value("GMT").toString();
    if (!dts.endsWith('Z')) dts += 'Z';
    setGMT(QDateTime::fromString(dts, Qt::ISODate));

    setTimezone(file.value("timezone").toFloat());
    setLocation(QVector3D(file.value("lon").toFloat(),
                          file.value("lat").toFloat(),
                          file.value("z").toFloat()));
    setLocationName(file.value("placeTag").toString());
    setComment(file.value("comment").toString());

    //if (getType()==TypeEvents) {
    QList<QDateTime> dl;
    if (file.contains("eventList")) {
        auto vl = file.value("eventList").toList();
        for (const auto& v: qAsConst(vl)) {
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

    clearUnsavedState();
    if (/*!recalculate*/!isEmpty()) resumeUpdate()/*holdUpdateMembers = None*/;  // if empty file is just loaded, it will not be recalculated
    //resumeUpdate();
}

void
AstroFile::loadComposite(const AFileInfoList& names)
{
    suspendUpdate();
    setFileInfo(names.first());

    auto file = std::make_unique<QSettings>(fileName(), QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    file->setIniCodec(QTextCodec::codecForName("UTF-8"));
#endif

    auto dts = file->value("GMT").toString();
    if (!dts.endsWith('Z')) dts += 'Z';
    setGMT(QDateTime::fromString(dts, Qt::ISODate));
}

void
AstroFile::resumeUpdate()
{
    if (!holdUpdate) return;
    holdUpdate = false;
    change(holdUpdateMembers, false);
    holdUpdateMembers = None;
}

void
AstroFile::change(AstroFile::Members members,
                  bool affectChangedState)
{
    if (members == None) return;

    bool unsavedBefore = unsavedChanges;
    if (affectChangedState && !(members & ChangedState)) unsavedChanges = true;
    if (unsavedBefore != unsavedChanges) members |= ChangedState;

    if (!holdUpdate) {
        if (members & (GMT | Location | HouseSystem | Zodiac | AspectSet | AspectMode))
            recalculate();
        else if (members & Harmonic) {
            recalculateBaseChart();
        }

        emit changed(members);
    } else {
        holdUpdateMembers |= members;
    }
}

void
AstroFile::clearUnsavedState()
{
    if (hasUnsavedChanges()) {
        unsavedChanges = false;
        if (!holdUpdate)
            change(ChangedState);
        else if (holdUpdateMembers & ChangedState)
            holdUpdateMembers ^= ChangedState;
    }
}

void
AstroFile::setName(const QString&   name)
{
    if (getName() != name) {
        qDebug() << "Renamed file" << getName() << "->" << name;
        _fileInfo.setFile(name);
        change(Name);
    }
}

void
AstroFile::setType(const FileType   type)
{
    if (this->type != type) {
        this->type = type;
        change(Type);
    }
}

void
AstroFile::setGMT(const QDateTime& gmt)
{
    if (scope.inputData.GMT != gmt) {
        scope.inputData.GMT = gmt;
        change(GMT);
    }
}

void
AstroFile::setTimezone(const short& zone)
{
    if (scope.inputData.tz != zone) {
        scope.inputData.tz = zone;
        change(Timezone);
    }
}

void
AstroFile::setLocation(const QVector3D location)
{
    if (scope.inputData.location != location) {
        scope.inputData.location = location;
        change(Location);
    }
}

void
AstroFile::setLocationName(const QString &location)
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
        scope.inputData.houseSystem = system;
        change(HouseSystem);
    }
}

void
AstroFile::setZodiac(A::ZodiacId zod)
{
    if (getZodiac() != zod) {
        scope.inputData.zodiac = zod;
        change(Zodiac);
    }
}

void
AstroFile::setAspectSet(A::AspectSetId set, bool force)
{
    if (getAspectSet().id != set || force) {
        scope.inputData.aspectSet = set;
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

void
AstroFile::setHarmonic(double harmonic)
{
    if (getHarmonic() != harmonic) {
        scope.harmonic = harmonic;
        change(Harmonic);
    }
}

void
AstroFile::setEventList(const QList<QDateTime>& evl)
{
    if (getEventList() != evl) {
        _eventList = evl;
        change(EventList);
    }
}

void
AstroFile::recalculate()
{
    qDebug() << "Calculating file" << getName() << "...";
    scope = A::calculateAll(scope.inputData);
}

void
AstroFile::recalculateBaseChart()
{
    qDebug() << "Calculating base harmonic chart for file" << getName() << "...";
    A::calculateBaseChartHarmonic(scope);
}

void
AstroFile::recalculateHarmonics()
{
    qDebug() << "Calculating harmonics for file" << getName() << "...";
    //A::findHarmonics(scope);
}

void
AstroFile::destroy()
{
    if (getName().section(" ", -1).toInt() == counter)   // latest file
        --counter;                                         // decrement file counter

    qDebug() << "Deleted file" << getName();
    //deleteLater();
    emit destroyRequested();
}

/*AstroFile :: ~AstroFile()
 {
  if (getName().section(" ", -1).toInt() == counter)   // latest file
    --counter;                                         // decrement file counter

  qDebug() << "Deleted file" << getName();
 }*/



 /* =========================== ABSTRACT FILE HANDLER ================================ */

AstroFileHandler::AstroFileHandler(QWidget *parent) :
    QWidget(parent), Customizable()
{
    delayUpdate = false;
}

void
AstroFileHandler::setFiles(const AstroFileList& files)
{
    MembersList flags;
    int i = 0;

    for (AstroFile* file: files) {
        AstroFile* old = (f.count() >= i + 1) ? f[i] : nullptr;
        if (file == old) {
            flags.clear();
        } else {
            if (old) {
                old->disconnect(this, SLOT(fileUpdatedSlot(AstroFile::Members)));
                old->disconnect(this, SLOT(fileDestroyedSlot()));
            }

            if (file) {
                connect(file, SIGNAL(changed(AstroFile::Members)),
                        this, SLOT(fileUpdatedSlot(AstroFile::Members)));
                connect(file, SIGNAL(destroyRequested()),
                        this, SLOT(fileDestroyedSlot()));
                flags << file->diff(old);
            } else {
                flags.clear();
            }
        }

        i++;
    }

    f = files;

    if (isVisible() && !isAnyFileSuspended()) {
        delayMembers = blankMembers();
        filesUpdated(flags);
    } else {
        delayMembers = flags;
        delayUpdate = true;
    }

}

A::AspectList
AstroFileHandler::calculateAspects()
{
    auto& scope = file(0)->horoscope();
    const auto& input = scope.inputData;

    A::setOrbFactor(1);
    if (file(0)->focalPlanets().empty()) {
        scope.aspects =
                A::calculateAspects(A::getAspectSet(input.aspectSet),
                                    scope.planets);
        return scope.aspects;
    }

    A::AspectSetId aspset = -1;
    auto fp = file(0)->focalPlanets();
    const auto& curr(A::EventOptions::current());
    if (fp.size() < curr.patternsQuorum) {
        bool skip = fp.containsAny(A::Ingresses_Start, A::Ingresses_End);
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


        auto hpc = A::findClusters(hs, {&file(0)->horoscope().planetsOrig},
                                   qMax(size_t(2),fp.size()),
                                   skip? A::PlanetSet() : fp,
                                   false,
                                   false /*curr.patternsRestrictMoon*/,
                                   curr.expandShowOrb);
        for (const auto& h_pc: hpc) {
            const auto& pc = h_pc.second;
            for (const auto& p_c: pc) {
                const auto& pl = p_c.first;
                qDebug() << QString("H%1 %2 %3")
                            .arg(h_pc.first)
                            .arg(p_c.second)
                            .arg(pl.names().join('='));
                fp.insert(pl.begin(),pl.end());
            }
        }
        A::setOrbFactor(curr.expandShowOrb / A::harmonicsMaxQOrb());
    } else {
        aspset = MainWindow::theAstroWidget()->overrideAspectSet();
    }

    const auto& asps = A::getAspectSet(aspset == -1? input.aspectSet : aspset);
    A::ChartPlanetPtrMap planets;
    //A::setOrbFactor(curr.patternsSpreadOrb / A::harmonicsMaxQOrb());
    for (const auto& cpid : fp) {
        auto fid = cpid.fileId();
        if (fid < 0) continue;
        auto pp = file(fid)->horoscope().getPlanet(cpid.planetId());
        planets.emplace(cpid, pp);
    }
    auto alist = A::calculateAspects(asps, planets);
    A::setOrbFactor(1);
    return alist;
}

A::AspectList
AstroFileHandler::calculateSynastryAspects()
{
    qDebug() << "Calculate synatry apects" << file(0)->getAspectSet().id;
    if (file(1)->focalPlanets().empty()) {
        A::setOrbFactor(0.25);
        return A::calculateAspects(file(0)->getAspectSet(),
                                   file(0)->horoscope().planets,
                                   file(1)->horoscope().planets);
    }

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

        A::PlanetProfile pf {
            &file(0)->horoscope().planetsOrig,
            &file(1)->horoscope().planetsOrig
        };
        A::setOrbFactor(curr.expandShowOrb / A::harmonicsMaxQOrb());
        QList<A::Aspect> alist;
        auto hpc = A::findClusters(hs, pf,
                                   qMax(size_t(2),fp.size()),
                                   skip? A::PlanetSet() : fp,
                                   true /*skipAllNatalOnly*/,
                                   false /*curr.patternsRestrictMoon*/,
                                   curr.expandShowOrb);
        for (const auto& h_pc: hpc) {
            auto h = h_pc.first;
            const auto& pc = h_pc.second;
            A::PlanetSet ps;
            for (const auto& p_c: pc) {
                const auto& pl = p_c.first;
                qDebug() << QString("H%1 %2 %3")
                            .arg(h_pc.first)
                            .arg(p_c.second)
                            .arg(pl.names().join('='));
                ps.insert(pl.begin(),pl.end());
            }
            A::ChartPlanetPtrMap planets;
            for (const auto& cpid : ps) {
                auto fid = cpid.fileId();
                if (fid < 0) continue;
                auto pp = file(fid)->horoscope().getPlanet(cpid.planetId());
                planets.emplace(cpid, pp);
            }
            const auto& asps = A::getAspectSet(A::topAspectSet().id + h);
            alist << A::calculateAspects(asps, planets);
        }
        A::setOrbFactor(1);
        return alist;
    } else {
        aspset = MainWindow::theAstroWidget()->overrideAspectSet();
        A::setOrbFactor(1);
    }

    const auto& asps = A::getAspectSet(aspset == -1
                                       ? file(0)->horoscope().inputData.aspectSet
                                       : aspset);
    //const auto& asps = aspset != -1? A::getAspectSet(aspset) : file(0)->getAspectSet();
    A::ChartPlanetPtrMap planets;
    for (const auto& cpid : fp) {
        auto fid = cpid.fileId();
        if (fid < 0) continue;
        auto pp = file(fid)->horoscope().getPlanet(cpid.planetId());
        planets.emplace(cpid, pp);
    }
    auto alist = A::calculateAspects(asps, planets);
    A::setOrbFactor(1);
    return alist;
}

MembersList
AstroFileHandler::blankMembers()
{
    MembersList ret;
    for (int i = 0; i < f.count(); i++)
        ret << AstroFile::Members();
    return ret;
}

bool
AstroFileHandler::isAnyFileSuspended()
{
    for (auto  file: qAsConst(f)) if (file->isSuspendedUpdate()) return true;
    return false;
}

void
AstroFileHandler::fileUpdatedSlot(AstroFile::Members m)
{
    int i = f.indexOf((AstroFile*)sender());
    if (i==-1) return; // file is not in set (yet?)

    if (isVisible() && !isAnyFileSuspended()) {
        MembersList mList;
        if (delayUpdate) {
            mList = delayMembers;
            delayMembers = blankMembers();
            delayUpdate = false;
        } else {
            mList = blankMembers();
        }

        while (mList.count()<=i) {
            mList.append(AstroFile::Members());
        }
        mList[i] |= m;
        filesUpdated(mList);
    } else {
        delayUpdate = true;
        while (delayMembers.count()<=i) {
            delayMembers.append(AstroFile::Members());
        }
        delayMembers[i] |= m;
    }
}

void
AstroFileHandler::fileDestroyedSlot()
{
    int i = f.indexOf((AstroFile*)sender());
    if (i == -1) return;                  // ignore if destroying file not in list (e.g. in other tab)

    MembersList mList = blankMembers();
    if (i < f.count() - 1)
        mList[i] = f[i + 1]->diff(f[i]);      // write difference with next file in list
    f.removeAt(i);
    mList.removeLast();
    filesUpdated(mList);
}

void
AstroFileHandler::resumeUpdate()
{
    if (delayUpdate) {
        filesUpdated(delayMembers);
        delayMembers = blankMembers();
        delayUpdate = false;
    }
}



/* =========================== ASTRO TREE VIEW ====================================== */

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
  addChildItem(tr("Saturn is harmoniously aspected and is disposited in V house"),
               h.saturn.house == 5 && hasHarmonicAspects(h.saturn, h));


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
    addChildItem(tr("Moon has aspect with Uranus - love affair with married woman"),
                 A::aspect(h.moon, h.uranus) != A::Aspect_None);
    addChildItem(tr("Venus has aspect with Uranus - free relationships"),
                 A::aspect(h.venus, h.uranus) != A::Aspect_None);
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

  addChildItem(tr("Marriage significator (%1) is wealth and strong").arg(ms.name),
               !hasDamage(ms, h) && hasHarmonicAspects(ms, h));
  addChildItem(tr("Sun is disposited in V or VII house"),
               h.sun.house == 5 || h.sun.house == 7);
  addChildItem(tr("Moon is disposited in V or VII house"),
               h.moon.house == 5 || h.moon.house == 7);


  addTopLevelItem(tr("Celibacy"));

  addChildItem(tr("Saturn is disposited in VII house"),
               h.saturn.house == 7);

  addChildItem(tr("Marriage significator (%1) is damaged by Saturn").arg(ms.name),
               A::aspect(ms, h.saturn) == A::Aspect_Quadrature ||
               A::aspect(ms, h.saturn) == A::Aspect_Opposition);

  addChildItem(tr("Ruler of VII house is damaged by Saturn"),
               A::aspect(A::ruler(7, h), h.saturn) == A::Aspect_Quadrature ||
               A::aspect(A::ruler(7, h), h.saturn) == A::Aspect_Opposition);

  addChildItem(tr("Uranus is disposited in VII house (excessive independence)"),
               h.uranus.house == 7);

  addChildItem(tr("Venus is located in major aspect with Neptune (excessive idealism)"),
               A::aspect(h.venus, h.neptune) != A::Aspect_None);


  addTopLevelItem(tr("Plural marriage"));

  addChildItem(tr("Asc-Dsc axis lays in mutable cross"),
               A::getSignNumber(h.houses.cusp[0]) % 3 == 0); // mutable signs are: 3, 6, 9, 12

  addChildItem(tr("Uranus or Pluto is disposited in VII house"),
               h.uranus.house == 7 || h.pluto.house == 7);

  addChildItem(tr("Marriage significator (%1) is located in Gemini, Saggitarius or Pisces")
                                                                  .arg(ms.name),
               ms.sign == 3 ||
               ms.sign == 9 ||
               ms.sign == 12);

  addChildItem(tr("Jupiter is disposited in VII house and is located in "
                  "Gemini, Saggitarius, Aquarius or Pisces"),
               h.jupiter.house == 7 && (h.jupiter.sign == 3  ||
                                        h.jupiter.sign == 9  ||
                                        h.jupiter.sign == 11 ||
                                        h.jupiter.sign == 12));

  addChildItem(tr("Ruler of VII house is located in major aspect with Uranus, Mercury or Moon"),
               A::aspect(A::ruler(7, h), h.uranus)  != A::Aspect_None ||
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

  addChildItem(tr("Ruler of VII house is located in major aspect with ruler of II house"),
               A::aspect(A::ruler(7,h),
                                 A::ruler(2,h)) != A::Aspect_None);

  addChildItem(tr("Ruler of VII house is disposited in IV house"),
               A::ruler(7, h).house == 4);

  addChildItem(tr("Ruler of VII house is located in major aspect with ruler of IV house"),
               A::aspect(A::ruler(7,h),
                                 A::ruler(4,h)) != A::Aspect_None);

  addChildItem(tr("Ruler of VII house is disposited in X house"),
               A::ruler(7, h).house == 10);

  addChildItem(tr("Marriage significator (%1) is located in tense aspect with Neptune")
                  .arg(ms.name),
               A::aspect(ms, h.neptune) == A::Aspect_Quadrature ||
               A::aspect(ms, h.neptune) == A::Aspect_Opposition);

  addChildItem(tr("Sun or Moon is located in tense aspect with Neptune"),
               A::aspect(h.sun,  h.neptune) == A::Aspect_Quadrature ||
               A::aspect(h.sun,  h.neptune) == A::Aspect_Opposition ||
               A::aspect(h.moon, h.neptune) == A::Aspect_Quadrature ||
               A::aspect(h.moon, h.neptune) == A::Aspect_Opposition);

  addChildItem(tr("Ruler of VII house is located in major aspect with ruler of X house"),
               A::aspect(A::ruler(7, h),
                                 A::ruler(10,h)) != A::Aspect_None);


  addTopLevelItem(tr("Childlessness"));

  addChildItem(tr("Saturn is disposited in V house"),
               h.saturn.house == 5);

  addChildItem(tr("Ruler of V house is located in tense aspect with Saturn"),
               A::aspect(A::ruler(5, h), h.saturn) == A::Aspect_Quadrature ||
               A::aspect(A::ruler(5, h), h.saturn) == A::Aspect_Opposition);

  addChildItem(tr("Moon is disposited in V house and is located in tense aspect with Saturn"),
               h.moon.house == 5 && (A::aspect(h.moon, h.saturn) == A::Aspect_Quadrature ||
                                        A::aspect(h.moon, h.saturn) == A::Aspect_Opposition));



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
    addChildItem(tr("Sun is located in Taurus (or water sign) and is disposited in V house"),
                 h.sun.house == 5 && (h.sun.sign == 2 ||
                                      h.sun.sign == 4 ||
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

    addChildItem(tr("Mars, Uranus or Pluto is damaged by other planet and is disposited in V house"),
                 (h.mars.house   == 5 && hasDamage(h.mars,   h)) ||
                 (h.uranus.house == 5 && hasDamage(h.uranus, h)) ||
                 (h.pluto.house  == 5 && hasDamage(h.pluto,  h)));
   }



  addTopLevelItem(tr("Children in foster care"));

  addChildItem(tr("Ruler of V house is wealth and is diposited in III house, or vice-versa"),
               (A::rulerDisposition(5, 3, h) && !hasDamage(A::ruler(3,h), h)) ||
               (A::rulerDisposition(3, 5, h) && !hasDamage(A::ruler(5,h), h)));




  addTopLevelItem(tr("Children out of wedlock"));

  addChildItem(tr("Neptune is wealth and is disposited in V house"),
               h.neptune.house == 5 && !hasDamage(h.neptune, h));

  addChildItem(tr("Ruler of V house is wealth and is diposited in XII house, or vice-versa"),
               (A::rulerDisposition(5, 12, h) && !hasDamage(A::ruler(12,h), h)) ||
               (A::rulerDisposition(12, 5, h) && !hasDamage(A::ruler(5,h), h)));



  addTopLevelItem(tr("Separation from child"));

  addChildItem(tr("Neptune is damaged and is disposited in V house"),
               h.neptune.house == 5 && hasDamage(h.neptune, h));

  addChildItem(tr("Ruler of V house is damaged and is diposited in XII house, or vice-versa"),
               (A::rulerDisposition(5, 12, h) && hasDamage(A::ruler(12,h), h)) ||
               (A::rulerDisposition(12, 5, h) && hasDamage(A::ruler(5,h), h)));



  addTopLevelItem(tr("Death of a child"));

  addChildItem(tr("Saturn or Pluto is disposited in V house"),
               h.saturn.house == 5 || h.pluto.house == 5);

  bool b = false;
  foreach (const A::Planet& p, h.planets)
    if (p.house == 8 && (A::aspect(p, A::ruler(5,h)) == A::Aspect_Opposition ||
                            A::aspect(p, A::ruler(5,h)) == A::Aspect_Quadrature))
      b = true;

  addChildItem(tr("Ruler of V house is in quadrature or opposition to element of VIII house"),
               b);

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
  bool sunAbove = file->horoscope().sun.horizontalPos.y() > 0;  // sun is above the horizon

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

bool AstroTreeView :: hasDamage (const A::Planet& planet, const A::Horoscope &scope)
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

bool AstroTreeView :: hasHarmonicAspects (const A::Planet& planet, const A::Horoscope &scope)
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

/* =========================== ASTRO TOPICS SHOW ==================================== */

/*AstrotTopicsShow :: AstrotTopicsShow(QWidget *parent) : AstroFileHandler(parent)
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
