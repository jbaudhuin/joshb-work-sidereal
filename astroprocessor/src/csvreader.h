#ifndef CSVREADER_H
#define CSVREADER_H

#include <QFile>
#include <QStringList>

class CsvFile : public QFile
{
    private:
        QStringList firstRow, currentRow;
        QFile file;

    public:
        // Resolve a (possibly relative) data path so the file is found
        // regardless of the process working directory. Anchors to the
        // executable directory first, then the cwd, then a bin/ subdir.
        // Also used to locate the Swiss Ephemeris "swe" directory.
        static QString resolvePath(const QString& name);

        CsvFile(const QString& name = "");

        void setFileName(const QString &name) { file.setFileName(resolvePath(name)); }
        bool openForRead();
        const QStringList& headerLabels();
        bool readRow();
        const QString& header(int column) { return headerLabels()[column]; }
        const QString& row(int column)    { return currentRow[column]; }
        int columnsCount()                { return currentRow.count(); }
        void close();

};

#endif // CSVREADER_H
