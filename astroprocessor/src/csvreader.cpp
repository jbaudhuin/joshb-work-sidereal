#include "csvreader.h"
#include <QCoreApplication>
#include <QDir>

QString CsvFile::resolvePath(const QString& name)
{
    if (name.isEmpty()) return name;

    // Absolute paths are used verbatim.
    if (QDir::isAbsolutePath(name)) return name;

    // Try the executable directory first so the data files are found no
    // matter what working directory the process was launched with, then
    // fall back to the working directory and a bin/ subdirectory.
    const QStringList candidates {
        QCoreApplication::applicationDirPath() + QStringLiteral("/") + name,
        QDir::currentPath() + QStringLiteral("/") + name,
        QDir::currentPath() + QStringLiteral("/bin/") + name,
    };
    for (const QString& path : candidates) {
        if (QFile::exists(path)) return path;
    }

    // Nothing found: return the executable-relative path so the resulting
    // "missing file" diagnostics point at the expected install location.
    return candidates.first();
}

CsvFile::CsvFile(const QString& name)
{
    file.setFileName(resolvePath(name));
}

bool CsvFile::openForRead()
{
    if (bool ret = file.open(QIODevice::ReadOnly)) {
        headerLabels();
        return ret;
    }
    return false;
}

const QStringList& CsvFile::headerLabels()
 {
  if (!firstRow.count())
   {
    readRow();
    firstRow = currentRow;
   }

  return firstRow;
 }

bool CsvFile::readRow()
{
    if (!file.isOpen() && !openForRead()) {
        return false;
    }
    while (1) {
        if (file.atEnd())
        {
            currentRow.clear();
            return false;
        }
        QString str = file.readLine().trimmed();
        currentRow = str.split(';');
        if (!str.isEmpty()) return true;
    }
}

void CsvFile::close()
{
    file.close();
    firstRow.clear();
    currentRow.clear();
}
