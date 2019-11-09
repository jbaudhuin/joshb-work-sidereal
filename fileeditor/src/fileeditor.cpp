#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
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

    QPushButton* ok     = new QPushButton(tr("OK"));
    QPushButton* cancel = new QPushButton(tr("Cancel"));
    QPushButton* apply  = new QPushButton(tr("Apply"));

    tabs     -> setTabsClosable(true);
    tabs     -> setMovable(true);
    addFileBtn -> setMaximumWidth(32);
    type     -> addItem(tr("male"),      AstroFile::TypeMale);
    type     -> addItem(tr("female"),    AstroFile::TypeFemale);
    type     -> addItem(tr("undefined"), AstroFile::TypeOther);
    timeZone -> setRange(-12, 12);
    timeZone -> setDecimals(1);

    dateTime -> setCalendarPopup(true);
    QString fmt = dateTime->displayFormat();
    if (fmt.replace("h:mm ", "h:mm:ss ")
            != dateTime->displayFormat())
    {
        // if we don't show seconds, they will be non-zero and
        // the only way to edit it is hand-editing the .dat file.
        dateTime->setDisplayFormat(fmt);
    }

    comment  -> setMaximumHeight(70);
    this     -> setWindowTitle(tr("Edit entry"));
    this     -> setWindowFlags(Qt::Dialog |
                               Qt::MSWindowsFixedSizeDialogHint |
                               Qt::WindowStaysOnTopHint);

    QHBoxLayout* lay4 = new QHBoxLayout;
    lay4->addWidget(tabs);
    lay4->addWidget(addFileBtn);

    QHBoxLayout* lay3 = new QHBoxLayout;
    lay3->addWidget(name);
    lay3->addWidget(new QLabel(tr("Gender:")));
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

    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addSpacerItem(new QSpacerItem(1,1, QSizePolicy::Expanding, QSizePolicy::Preferred));
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    buttons->addWidget(apply);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(lay4);
    layout->addLayout(lay1);
    layout->addLayout(buttons);


    connect(tabs,       SIGNAL(currentChanged(int)),    this, SLOT(currentTabChanged(int)));
    connect(tabs,       SIGNAL(tabMoved(int,int)),      this, SLOT(swapFilesSlot(int,int)));
    connect(tabs,       SIGNAL(tabCloseRequested(int)), this, SLOT(removeTab(int)));
    connect(addFileBtn, SIGNAL(clicked()), this, SIGNAL(appendFile()));
    connect(ok,         SIGNAL(clicked()), this, SLOT(applyToFile()));
    connect(apply,      SIGNAL(clicked()), this, SLOT(applyToFile()));
    connect(ok,         SIGNAL(clicked()), this, SLOT(close()));
    connect(cancel,     SIGNAL(clicked()), this, SLOT(close()));
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
    if (currentFile == index) return;
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

void AstroFileEditor::applyToFile()
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
    dst->resumeUpdate();
}

/*void AstroFileEditor::showEvent(QShowEvent* e)
 {
  AstroFileHandler::showEvent(e);
  if (!secondFile()) reset2ndFile();
 }*/
