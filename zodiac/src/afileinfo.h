#ifndef AFILEINFO_H
#define AFILEINFO_H

#include <QFileInfo>
#include <QList>
#include <QMetaType>

class AFileInfo : public QFileInfo
{
public:
    AFileInfo();

    AFileInfo(const AFileInfo& from) : QFileInfo(from) { }

    AFileInfo(const QDir& dir, const QString& file) :
        QFileInfo(dir, encodeName(file) + suff())
    { }

    AFileInfo(const QString& file) :
        QFileInfo(file.endsWith(suff(), Qt::CaseInsensitive) ? file : (encodeName(file) + suff()))
    { }

    //AFileInfo& operator=(AFileInfo&& other);
    AFileInfo& operator=(const AFileInfo& other)
    { QFileInfo::operator=(other); return *this; }

    // The chart's display name: the file name minus OUR suffix, decoded.
    // Deliberately not QFileInfo::baseName(), which cuts at the FIRST dot
    // and so turned "James L. Sullivan" into "James L".
    QString baseName() const
    {
        QString n = fileName();
        if (n.endsWith(suff(), Qt::CaseInsensitive)) n.chop(suff().length());
        return decodeName(n);
    }

    using QFileInfo::setFile;
    void setFile(const QString& file) { QFileInfo::setFile(encodeName(file) + suff()); }

    static QString encodeName(const QString& from);
    static QString decodeName(const QString& from);

    static const QString& suff() { static QString _suff(".dat"); return _suff; }
    static const QStringList& wildcard()
    { static QStringList _wc = QStringList() << "*" + suff(); return _wc; }
};

typedef QList<AFileInfo> AFileInfoList;

Q_DECLARE_METATYPE(AFileInfo)
Q_DECLARE_METATYPE(AFileInfoList)

#endif // AFILEINFO_H
