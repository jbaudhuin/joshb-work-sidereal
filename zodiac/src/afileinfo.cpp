#include "afileinfo.h"
#include <QRegularExpression>

AFileInfo::AFileInfo()
{

}

namespace {
const QRegularExpression escre("[/\\\\:%=@\\$]"); // {/\:%=@$}
const QRegularExpression restore("%[0-9a-zA-Z][0-9a-zA-Z]");
}

/*static*/
QString
AFileInfo::encodeName(const QString& from)
{
    auto rem = escre.globalMatch(from);
    if (!rem.hasNext()) return from;

    QString ret;
    int p = 0;
    while (rem.hasNext()) {
        auto m = rem.next();
        if (p < m.capturedStart()) ret += from.mid(p,m.capturedStart()-p);
        ret += '%' + QString("%1").arg(int(m.captured()[0].cell()),2,16,QChar('0'));
        p = m.capturedEnd();
    }
    if (p < from.length()) ret += from.mid(p);
    return ret;
}


/*static*/
QString
AFileInfo::decodeName(const QString& from)
{
    auto rem = restore.globalMatch(from);
    if (!rem.hasNext()) return from;

    QString ret;
    int p = 0;
    while (rem.hasNext()) {
        auto m = rem.next();
        if (p < m.capturedStart()) ret += from.mid(p,m.capturedStart()-p);
        bool ok = false;
        ret += QChar(m.captured().mid(1).toUInt(&ok,16));
        p = m.capturedEnd();
    }
    if (p < from.length()) ret += from.mid(p);
    return ret;
}
