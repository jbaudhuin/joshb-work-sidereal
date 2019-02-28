#include "csvreader.h"
#include <QDir>

CsvFile::CsvFile(const QString& name)
{
    std::string cwd = QDir::currentPath().toStdString();
    file.setFileName(name);
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
