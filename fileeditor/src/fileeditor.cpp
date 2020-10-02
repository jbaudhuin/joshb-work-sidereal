#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonDocument>
#include "geosearch.h"
#include "fileeditor.h"


/* =========================== ASTRO FILE EDITOR ==================================== */

AstroFileEditor::AstroFileEditor(QWidget *parent) :
    AstroFileHandler(parent),
    _inUpdate(false)
{
    currentFile = 0;

    tabs              = new QTabBar;
    addFileBtn        = new QPushButton(tr("+"));
    name              = new QLineEdit;
    type              = new QComboBox;
    dateTime          = new QDateTimeEdit;
    timeZone          = new QDoubleSpinBox;
    geoSearch         = new GeoSearchWidget;
    comment           = new QPlainTextEdit;

    tabs     -> setTabsClosable(true);
    tabs     -> setMovable(true);
    addFileBtn -> setMaximumWidth(32);
    type->addItem(tr("male"),       AstroFile::TypeMale);
    type->addItem(tr("female"),     AstroFile::TypeFemale);
    type->addItem(tr("search"),     AstroFile::TypeSearch);
    type->addItem(tr("event(s)"),   AstroFile::TypeEvents);
    type->addItem(tr("other"),      AstroFile::TypeOther);
    timeZone -> setRange(-12, 12);
    timeZone -> setDecimals(1);

    dateTime -> setCalendarPopup(true);
    QString fmt = dateTime->displayFormat();
    if (fmt.replace("h:mm ", "h:mm:ss ") != dateTime->displayFormat()) {
        // if we don't show seconds, they will be non-zero and
        // the only way to edit it is hand-editing the .dat file.
        dateTime->setDisplayFormat(fmt);
    }
    dateTime->setMinimumDate(QDate(100,1,1));
    qDebug() << "Minimum date:" << dateTime->minimumDate();

    comment  -> setMaximumHeight(70);
    if (!parent) {
    this     -> setWindowTitle(tr("Edit entry"));
    this     -> setWindowFlags(Qt::Dialog |
                               Qt::MSWindowsFixedSizeDialogHint |
                               Qt::WindowStaysOnTopHint);
    }

    QHBoxLayout* lay4 = new QHBoxLayout;
    lay4->addWidget(tabs);
    lay4->addWidget(addFileBtn);

    QHBoxLayout* lay3 = new QHBoxLayout;
    lay3->addWidget(name);
    lay3->addWidget(new QLabel(tr("Type:")));
    lay3->addWidget(type);

    QHBoxLayout* lay2 = new QHBoxLayout;
    lay2->addWidget(dateTime);
    lay2->addWidget(new QLabel(tr("Time zone:")));
    lay2->addWidget(timeZone);

    QFormLayout* lay1 = new QFormLayout;
    lay1->addRow(tr("Name:"),       lay3);
    lay1->addRow(tr("Local time:"), lay2);
    lay1->addRow(tr("Location:"),   geoSearch);
    lay1->addItem(new QSpacerItem(1,20));
    lay1->addRow(tr("Comment:"),    comment);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(lay4);
    layout->addLayout(lay1);

    if (!parent) {
        QPushButton* ok     = new QPushButton(tr("OK"));
        QPushButton* cancel = new QPushButton(tr("Cancel"));
        QPushButton* apply  = new QPushButton(tr("Apply"));

        QHBoxLayout* buttons = new QHBoxLayout;
        buttons->addSpacerItem(new QSpacerItem(1,1, QSizePolicy::Expanding, QSizePolicy::Preferred));
        buttons->addWidget(ok);
        buttons->addWidget(cancel);
        buttons->addWidget(apply);

        layout->addLayout(buttons);

        connect(ok,         SIGNAL(clicked()), this, SLOT(applyToFile()));
        connect(apply,      SIGNAL(clicked()), this, SLOT(applyToFile()));
        connect(ok,         SIGNAL(clicked()), this, SLOT(close()));
        connect(cancel,     SIGNAL(clicked()), this, SLOT(close()));
    }

    connect(tabs,       SIGNAL(currentChanged(int)),    this, SLOT(currentTabChanged(int)));
    connect(tabs,       SIGNAL(tabMoved(int,int)),      this, SLOT(swapFilesSlot(int,int)));
    connect(tabs,       SIGNAL(tabCloseRequested(int)), this, SLOT(removeTab(int)));
    connect(addFileBtn, SIGNAL(clicked()), this, SIGNAL(appendFile()));
    connect(timeZone,   SIGNAL(valueChanged(double)),
            this, SLOT(timezoneChanged()));

    connect(dateTime, SIGNAL(dateTimeChanged(const QDateTime&)),
            this, SLOT(updateTimezone()));
    connect(geoSearch, SIGNAL(locationChanged()),
            this, SLOT(updateTimezone()));

    dateTime -> setFocus();
}

void
AstroFileEditor::timezoneChanged()
{
    if (timeZone->value() < 0)
        timeZone->setPrefix("");
    else
        timeZone->setPrefix("+");
}

void
AstroFileEditor::updateTimezone()
{
    if (_inUpdate) return;

    QVector3D vec(geoSearch->location());

    auto nm = new QNetworkAccessManager(this);
    connect(nm, &QNetworkAccessManager::finished,
            [this](QNetworkReply* reply)
    {
        reply->deleteLater();
        if (auto nm = sender()) {
            nm->deleteLater();
        }
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument response =
                QJsonDocument::fromJson(reply->readAll());
        if (response["status"].toString()!="OK") {
            qDebug() << "Timezone request failed:"
                     << response["status"].toString()
                     << response["errorMessage"].toString();
            return;
        }
        qreal tz = (response["rawOffset"].toInt()
                + response["dstOffset"].toInt())/3600;
        timeZone->setValue(tz);

        qDebug() << "Timezone for location is"
                 << response["rawOffset"].toInt()/60 /*minsPerSec*/
                 << "with dstOffset"
                 << response["dstOffset"].toInt()/60
                 << "in" << response["timeZoneName"].toString();
    });

    QString url =
            QString(A::googMapURL + "/timezone/json?"
                                "location=%1,%2"
                                "&timestamp=%4"
                                "&key=%3"
                                "&language=en")
            .arg(vec.y()).arg(vec.x())
            .arg(A::googAPIKey)
            .arg(dateTime->dateTime().toSecsSinceEpoch());
    qDebug() << "Issuing TZ URL:" << url;
    nm->get(QNetworkRequest(url));

}

void AstroFileEditor::updateTabs()
{
    for (int i = 0; i < filesCount(); i++) {
        QString txt = file(i)->getName();
        if (tabs->count() <= i)
            tabs->addTab(txt);
        else
            tabs->setTabText(i, txt);
    }

    // remove unused tabs
    for (int i = filesCount(); i < tabs->count(); i++)
        tabs->removeTab(i);

    addFileBtn->setVisible(filesCount() < 2);
}

void AstroFileEditor::update(AstroFile::Members m)
{
    qDebug() << "AstroFileEditor: show file" << currentFile;
    AstroFile* source =  file(currentFile);

    A::modalize<bool> inUpdate(_inUpdate,true);
    name->setText(source->getName());
    geoSearch->setLocation(source->getLocation(),
                           source->getLocationName());
    timeZone->setValue(source->getTimezone());
    dateTime->setDateTime(source->getLocalTime());
    comment->setPlainText(source->getComment());

    if (m & AstroFile::Type) {
        for (int i = 0; i < type->count(); i++)
            if (type->itemData(i) == source->getType())
                type->setCurrentIndex(i);
    }
}

void AstroFileEditor::currentTabChanged(int index)
{
    if (currentFile == index || index==-1) return;
    int oldFile = currentFile;
    currentFile = index;
    update(file(index)->diff(file(oldFile)));
}

void AstroFileEditor::removeTab(int index)
{
    if (filesCount() < 2) return;
    //currentFile = 0;
    tabs->removeTab(index);
    file(index)->destroy();
}

void AstroFileEditor::swapFilesSlot(int i, int j)
{
    currentFile = i;
    emit swapFiles(i, j);
}

void AstroFileEditor::setCurrentFile(int index)
{
    tabs->setCurrentIndex(index);
}

void AstroFileEditor::filesUpdated(MembersList members)
{
    updateTabs();
    if (!filesCount())
        close();
    else if (currentFile >= filesCount())
        setCurrentFile(0);
    else if(members[currentFile])
        update(members[currentFile]);
}

void AstroFileEditor::applyToFile(bool setNeedsSaveFlag /*=true*/,
                                  bool resume /*=true*/)
{
    AstroFile* dst = file(currentFile);

    dst->suspendUpdate();
    dst->setName(name->text());
    dst->setType((AstroFile::FileType)type->itemData(type->currentIndex()).toInt());
    dst->setLocationName(geoSearch->locationName());
    dst->setLocation(geoSearch->location());
    dst->setTimezone(timeZone->value());
    QString ugh = dateTime->dateTime().addSecs(timeZone->value()*-3600)
            .toString(Qt::ISODate) + "Z";
    dst->setGMT(QDateTime::fromString(ugh,Qt::ISODate));
    dst->setComment(comment->document()->toPlainText());
    if (resume) dst->resumeUpdate();
    if (!setNeedsSaveFlag) dst->clearUnsavedState();
}

/*void AstroFileEditor::showEvent(QShowEvent* e)
 {
  AstroFileHandler::showEvent(e);
  if (!secondFile()) reset2ndFile();
 }*/

AstroFindEditor::AstroFindEditor(QWidget* parent /*=nullptr*/) :
    AstroFileEditor(parent),
    _inDateSelection(false)
{
    if (planets.empty()) {
        planets = QStringList({ "Sun", "Moon", "Mercury", "Venus", "Mars",
                        "Jupiter", "Saturn", "Uranus", "Neptune",
                        "Pluto", "Chiron", "Pallas", "Vesta", "Juno",
                        "Ceres" });
    }

    if (signs.empty()) {
        signs = QStringList({ "Aries", "Taurus", "Gemini", "Cancer",
                      "Leo", "Virgo", "Libra", "Scorpio",
                      "Sagittarius", "Capricorn", "Aquarius", "Pisces" });
    }

    endDateTimeCB = nullptr;
    if (auto fl = findChild<QFormLayout*>()) {
        //auto lay = new QHBoxLayout;
        endDateTime = new QDateTimeEdit;
        //lay->addWidget();
        endDateTimeCB = new QCheckBox(tr("End search:"));
        connect(endDateTimeCB, SIGNAL(toggled(bool)),
                endDateTime, SLOT(setEnabled(bool)));
        endDateTimeCB->setChecked(true);
        endDateTimeCB->setChecked(false);
        fl->insertRow(2, endDateTimeCB, endDateTime);

        int crow;
        QFormLayout::ItemRole role;
        fl->getWidgetPosition(comment, &crow, &role);
        auto lbll = fl->itemAt(crow, QFormLayout::LabelRole);
        auto lblw = lbll->widget();
        lblw->setHidden(true);
        comment->setHidden(true);

        auto lw = new QListWidget();
        lw->setSelectionMode(QAbstractItemView::SingleSelection);
        fl->addRow(tr("Found"), lw);

        connect(name, SIGNAL(editingFinished()),
                this, SLOT(onEditingFinished()));
        connect(dateTime, &QDateTimeEdit::dateTimeChanged,
                [this](const QDateTime& dt) {
            if (endDateTimeCB->isChecked()) return;
            endDateTime->blockSignals(true);
            endDateTime->setDateTime(dt.addYears(1));
            endDateTime->blockSignals(false);
        });
        connect(dateTime, SIGNAL(dateTimeChanged(const QDateTime&)),
                this, SLOT(onEditingFinished()));
        connect(endDateTime, SIGNAL(dateTimeChanged(const QDateTime&)),
                this, SLOT(onEditingFinished()));
        connect(endDateTimeCB, SIGNAL(toggled(bool)),
                this, SLOT(onEditingFinished()));

        auto emitter = [this, lw]
        { emit hasSelection(!lw->selectedItems().isEmpty()); };

        connect(lw, &QListWidget::itemSelectionChanged, emitter);
    }

#if 0
    QStringList planetsAndSigns(planets);
    planetsAndSigns.append(signs);

    QString prestr = "(" + planets.join("|") + ")";
    QString srestr =
            "(" + signs.join("|") + ")"
            + "[a-z]*((?<deg>\\d+)(\\.(?<min>\\d+)('((?<sec>\\d+)\"?)?)?)?)?";

    QString psrestr = "((?<otherPlanet>" + prestr + ")"
                      + "|(?<sign>" + srestr + "))";

    QString restr =
            QString("(H(?<harmonic>\\d+(\\.\\d+)?) )?")
            + "(?<planet>" + prestr + ")"
            + "(?<planetOrSign>=" + psrestr + ")";
#else
    QString plre = "\\w+(-\\w+)?";  // e.g., planet-r, planet-p
    QString plmpre = QString("(%1(/%1)?)").arg(plre);   // planet/planet
    QString signsre = "(" + signs.join("|") + ")";
    QString zposre = "(?<deg>\\d+)? ?(?<sign>"+ signsre +")"
                     "( ?((?<min>\\d+)'? ?((?<sec>\\d+)\"?)?))";
                                    // sign or deg Sign mins' sec"
    QString plmpeqre = plmpre + "(=" + plmpre + ")+";
                                    // e.g., sun=moon=mars
    QString plmpzposre = "(?<ingress>"
                         "(?<body>" + plmpre + ") ingress "
                         "(?<pos>" + zposre + ")"
                         ")";       // e.g., sun=15tau, sun=cap
    QString asprestr = "(?<aspect>" + plmpeqre + ")";
    QString stationre = "((?<station>(" + planets.join("|") + ")) station)";
    QString restr = "("
                    "(H(?<harmonic>\\d+(\\.\\d+)?) )?"
                    "(" + plmpzposre + "|" + asprestr + ")"
                    + "|" + stationre + ")";
#endif
    qDebug() << restr;
    QRegularExpression re(restr,QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid()) {
        qDebug() << re.errorString() << "at" << re.patternErrorOffset();
    } else {
        auto rev = new QRegularExpressionValidator(re, this);
        name->setValidator(rev);
    }

    name->setFocus();
}

void
AstroFindEditor::onEditingFinished()
{
    if (!name->hasAcceptableInput()) return;
    if (_inDateSelection) return;

    auto rev = qobject_cast<const QRegularExpressionValidator*>(name->validator());
    auto re = rev->regularExpression();
    auto match = re.match(name->text());
    if (!match.hasMatch()) return;

    auto capUInt = [&](const QString& cap) -> unsigned {
        auto caps = match.captured(cap);
        if (caps.isEmpty()) return 0;
        return caps.toUInt();
    };
    auto has = [&](const QString& cap) -> bool {
        return !match.captured(cap).isEmpty();
    };

    A::PlanetProfile poses;

    // get any edits to date or whatever
    AstroFileEditor::applyToFile(false/*not needsSave*/,
                                 false/*not recalc*/);
    A::InputData inda = file()->data();
    double orb, horb, span;

    if (has("station")) {
        auto planet = match.captured("station");
        auto pid = A::getPlanetId(planet);
        auto pla = A::getPlanet(pid);
        poses.push_back(new A::TransitPosition(pid, inda));
        span = 10;
    } else {
        inda.harmonic = 1;
        auto str = match.captured("harmonic");
        if (!str.isNull()) inda.harmonic = str.toDouble();

        if (has("ingress")) {
            auto n = match.captured("body");
            auto pid = A::getPlanetId(n);
            auto pla = A::getPlanet(pid);
            auto sgn = match.captured("sign");
            unsigned deg = capUInt("deg");
            unsigned min = capUInt("min");
            unsigned sec = capUInt("sec");
            auto pos = A::getSignPos(inda.zodiac, sgn, deg, min, sec);
            poses.push_front(new A::Loc(match.captured("ingress"), pos));
            poses.push_back(new A::TransitPosition(pid, inda));
        } else if (has("aspect")) {
            auto asp = match.captured("aspect");
            for (const auto& pln : asp.split("=")) {
                if (pln.indexOf('/') == -1) {
                    auto pid = A::getPlanetId(pln);
                    if (pid == A::Planet_None) continue;
                    auto pla = A::getPlanet(pid);
                    poses.push_back(new A::TransitPosition(pid, inda));
                    continue;
                }
                auto mpls = pln.split('/');
                auto pid1 = A::getPlanetId(mpls.takeFirst());
                auto pid2 = A::getPlanetId(mpls.takeFirst());
                auto cpid = A::ChartPlanetId(pid1,pid2);
                poses.push_back(new A::TransitPosition(cpid, inda));
            }
        }

        A::calculateOrbAndSpan(poses, inda, orb, horb, span);
    }

    auto d0 = A::getJulianDate(dateTime->dateTime());
    auto d1 = A::getJulianDate(endDateTime->dateTime());
    auto dl = A::quotidianSearch(poses, inda,
                                 endDateTime->dateTime(),
                                 std::min((d1-d0)/8,span));

    auto lw = findChild<QListWidget*>();
    lw->clear();
    QTimeZone tz(timeZone->value()*3600);

    for (auto dt : dl) {
        dt = dt.addSecs(timeZone->value()*3600);
        dt.setTimeZone(tz); // @todo compute using googleapi
        lw->addItem(dt.toString(QLocale().dateTimeFormat(QLocale::LongFormat)));
    }
}

void
AstroFindEditor::applyToFile()
{
    auto lw = findChild<QListWidget*>();
    auto sel = lw->selectedItems();
    if (sel.isEmpty()) return;

    A::modalize<bool> inReset(_inDateSelection, true);
    auto item = sel.takeFirst();
    auto dt = QLocale().toDateTime(item->text(), QLocale::LongFormat);
    dateTime->setDateTime(dt);
    AstroFileEditor::applyToFile(false/*not needs-save*/);
}
