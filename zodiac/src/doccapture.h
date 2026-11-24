#pragma once

// Developer-only "doc capture" mode: pick any widget under the mouse and save
// it as a high-DPI PNG for the help site. Compiled in only with the CMake
// option ZODIAC_DOC_CAPTURE (qmake: CONFIG+=doc_capture) and dormant unless the
// app is started with --doc-capture. See docs/doc-capture.md.

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "widgetpicker.h"

class CaptureOutline;
class DocCaptureScript;
class QWidget;

class DocCapture : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString outDir;
        qreal   dpr     = 2.0;
        int     delayMs = 3000;
        QString manifest;   // --doc-capture-manifest= / --doc-capture-script=
        bool    runScript = false; // batch-run the manifest, then quit
        QString imgDir;     // where manifest shots go; default <manifest>/docs/assets/img
        QString only;       // regex on manifest file names (script runs)
    };

    // Installs the qApp event filter when args contain --doc-capture; returns
    // nullptr (and does nothing at all) otherwise.
    static DocCapture* installIfRequested(const QStringList& args);
    static Options     optionsFromEnvAndArgs(const QStringList& args);

    ~DocCapture() override;

signals:
    // Hooks for a future callout / F1-help mode built on the same picking.
    void targetChanged(QWidget* target);
    void targetActivated(QWidget* target);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    friend class DocCaptureScript; // renders and saves through the same path

    explicit DocCapture(const Options& opt, QObject* parent);

    void     setActive(bool on);
    void     tick();
    void     retarget(QWidget* leaf);
    void     step(int dir);
    QWidget* target() const;
    void     updateLabel();

    void    startCapture(bool withPopups, bool delayed);
    bool    capture(QWidget*       w,
                    bool           withPopups,
                    const QString& mode,
                    const QString& stem = QString());
    // grab(): the PNG-ready image (DPR recorded) and the logical area it covers.
    QImage  grab(QWidget* w, bool withPopups, QRect* area) const;
    // save(): writes img to path, appends to captures.jsonl; empty on success.
    QString save(QWidget* w, const QImage& img, const QRect& area,
                 const QString& path, const QString& mode) const;
    QImage  renderWidget(QWidget* w) const;
    QImage  renderWithPopups(QWidget* w, QRect* logicalRect) const;
    QString nextFreePath(const QString& stem) const;
    void    appendLog(const WidgetPicker::Descriptor& d,
                      const QString&                  file,
                      const QImage&                   img,
                      const QString&                  mode,
                      const QString&                  helpPage) const;
    void    toast(const QString& msg);

    Options                   _opt;
    DocCaptureScript*         _script  = nullptr; // manifest / script mode
    CaptureOutline*           _outline = nullptr;
    QTimer                    _tick;
    QList<QPointer<QWidget>>  _chain;
    int                       _depth  = 0;
    bool                      _active = false;
    bool                      _frozen = false;
    QPoint                    _lastPos;
    int                       _wheelAccum = 0;
    QString                   _toast;
    QElapsedTimer             _toastTimer;

    // Delayed capture (Shift+F12): tooltips and hover states vanish on a key
    // press, so the shot is taken after a countdown instead.
    QElapsedTimer _countdown;
    bool          _pending           = false;
    bool          _pendingWithPopups = false;
};
