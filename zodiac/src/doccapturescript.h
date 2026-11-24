#pragma once

// Manifest-driven doc captures (help/shots.json), on top of DocCapture.
//
//  --doc-capture-manifest=<shots.json>   interactive: the outline label shows
//      the current entry; F12 saves it under the entry's file name and records
//      the picked widget as its target; Ctrl+PgUp/PgDn change entry; F10 runs
//      the entry's setup steps.
//  --doc-capture-script=<shots.json>     batch: run every auto entry that has
//      a target, write script-report.json, and quit (exit code 1 on failures).
//      Requires --data-dir so no personal charts or sessions can appear.
//
// Setup steps and shot fields are documented in docs/doc-capture.md.

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <functional>

class DocCapture;
class QWidget;

class DocCaptureScript : public QObject
{
    Q_OBJECT

public:
    explicit DocCaptureScript(DocCapture* dc);

    bool load();        // read the manifest; false (and a toast) on error
    void startBatch();  // load, then run all shots once the window is up

    // Interactive manifest pass.
    bool    hasEntry() const { return _current >= 0 && _current < _shots.size(); }
    void    stepEntry(int dir);
    QString entryLine() const;
    void    captureEntry(QWidget* w, bool withPopups);
    void    runCurrentSetup();

private:
    using Done = std::function<void(const QString& error)>;

    QJsonObject shot(int i) const { return _shots.at(i).toObject(); }
    QString     imagePath(const QJsonObject& s) const;
    bool        writeBack(int i, const QJsonObject& s);

    // Step engine: runs steps one at a time on the event loop. Each step's
    // continuation is queued *before* the step runs, so a step that opens a
    // modal dialog or a menu (exec()) keeps the script going inside that
    // nested loop.
    void    runSteps(const QJsonArray& steps, Done done);
    void    nextStep();
    QString execStep(const QString& action, const QJsonValue& arg);
    void    waitUntil(std::function<bool()> ready, int timeoutMs, const QString& what);
    void    finishSteps(const QString& error);

    // Batch.
    void runNextShot();
    void runShotSteps(const QJsonObject& s);
    void captureShot(const QJsonObject& s);
    void cleanupAfterShot();
    void finishBatch();
    void report(const QString& file, const QString& status, const QString& detail);

    DocCapture* _dc;
    QString     _manifest;
    QJsonObject _root;
    QJsonObject _defaults;
    QJsonArray  _shots;
    int         _current = -1;

    // Running step list.
    QJsonArray    _steps;
    int           _stepIndex = 0;
    QString       _stepError;
    Done          _stepsDone;
    bool          _running = false;
    QTimer        _poll;
    QElapsedTimer _waitClock;

    // Batch state.
    bool         _batch    = false;
    int          _batchPos = -1;
    QString      _theme;
    QJsonArray   _shotSteps; // current shot: theme/size + its setup
    bool         _retried = false;
    QJsonArray   _results;
    int          _failures = 0;
};
