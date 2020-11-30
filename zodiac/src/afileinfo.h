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
        QFileInfo(dir, encodeName(file))
    { }

    AFileInfo(const QString& file) :
        QFileInfo(encodeName(file))
    { }

    //AFileInfo& operator=(AFileInfo&& other);
    AFileInfo& operator=(const AFileInfo& other)
    { QFileInfo::operator=(other); return *this; }

    QString baseName() const { return decodeName(QFileInfo::baseName()); }

    using QFileInfo::setFile;
    void setFile(const QString& file) { QFileInfo::setFile(encodeName(file)); }

    static QString encodeName(const QString& from);
    static QString decodeName(const QString& from);
};

typedef QList<AFileInfo> AFileInfoList;

Q_DECLARE_METATYPE(AFileInfo)
Q_DECLARE_METATYPE(AFileInfoList)

#endif // AFILEINFO_H
