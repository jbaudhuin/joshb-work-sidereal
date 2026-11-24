#include "doccapture.h"
#include "doccapturescript.h"
#include "helptopics.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPainter>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <climits>

using namespace WidgetPicker;

namespace
{

const char* const HelpLine =
    "F12 save · Shift+F12 delayed · Alt+F12 with popups · "
    "wheel / PgUp / PgDn level · F11 freeze · Esc exit";

// The outline is drawn by separate always-on-top, input-transparent windows
// rather than a child overlay widget: the Planets slide embeds a native
// QQuickView that would paint over any child widget, and popups and floating
// docks are windows of their own. Being separate windows also keeps the
// outline out of every render() capture.
class OverlayWindow : public QWidget
{
public:
    OverlayWindow()
        : QWidget(nullptr,
                  Qt::ToolTip | Qt::FramelessWindowHint |
                      Qt::WindowTransparentForInput |
                      Qt::WindowDoesNotAcceptFocus |
                      Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_QuitOnClose, false);
        setFocusPolicy(Qt::NoFocus);
        setProperty(OverlayProperty, true);
    }

    void place(const QRect& r)
    {
        if (geometry() != r) setGeometry(r);
        if (!isVisible()) show();
    }
};

// One side of the marching-ants rectangle.
class EdgeStrip : public OverlayWindow
{
public:
    static constexpr int Thickness = 2;
    static constexpr int Dash      = 5;

    explicit EdgeStrip(bool vertical) : _vertical(vertical) {}

    void setOffset(int o)
    {
        _offset = o;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter  p(this);
        const int period = Dash * 2;
        const int len    = _vertical ? height() : width();
        p.fillRect(rect(), Qt::white);
        for (int s = (_offset % period) - period; s < len; s += period) {
            p.fillRect(_vertical ? QRect(0, s, width(), Dash)
                                 : QRect(s, 0, Dash, height()),
                       Qt::black);
        }
    }

private:
    bool _vertical;
    int  _offset = 0;
};

// Caption under the outline: what is selected, and save confirmations.
class LabelWindow : public OverlayWindow
{
public:
    void setText(const QString& line1, const QString& line2, bool accent)
    {
        if (line1 == _line1 && line2 == _line2 && accent == _accent) return;
        _line1  = line1;
        _line2  = line2;
        _accent = accent;
        const QFontMetrics fm(font());
        resize(std::max(fm.horizontalAdvance(_line1),
                        fm.horizontalAdvance(_line2)) + 2 * Pad,
               2 * fm.height() + 2 * Pad);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter           p(this);
        const QFontMetrics fm(font());
        p.fillRect(rect(), QColor(32, 32, 32));
        p.setPen(Qt::white);
        p.drawText(Pad, Pad + fm.ascent(), _line1);
        p.setPen(_accent ? QColor(120, 230, 120) : QColor(190, 190, 190));
        p.drawText(Pad, Pad + fm.height() + fm.ascent(), _line2);
    }

private:
    static constexpr int Pad = 4;
    QString              _line1, _line2;
    bool                 _accent = false;
};

QColor
backgroundFor(const QWidget* w)
{
    // Many stylesheet-themed children paint no background of their own; fill
    // with the nearest ancestor's so the PNG has no transparent holes.
    for (const QWidget* a = w; a; a = a->parentWidget()) {
        if (a->autoFillBackground() || a->isWindow() ||
            a->testAttribute(Qt::WA_StyledBackground))
            return a->palette().color(a->backgroundRole());
    }
    return QApplication::palette().color(QPalette::Window);
}

QRect
globalRect(const QWidget* w)
{
    return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
}

} // namespace

class CaptureOutline : public QObject
{
public:
    explicit CaptureOutline(QObject* parent) : QObject(parent)
    {
        for (int i = 0; i < 4; ++i) _edges[i] = new EdgeStrip(i >= 2);
        _label = new LabelWindow;
    }

    ~CaptureOutline() override
    {
        for (EdgeStrip* e : _edges) delete e;
        delete _label;
    }

    void setRect(const QRect& r)
    {
        const int t = EdgeStrip::Thickness;
        _edges[0]->place(QRect(r.left() - t, r.top() - t, r.width() + 2 * t, t));
        _edges[1]->place(QRect(r.left() - t, r.bottom() + 1, r.width() + 2 * t, t));
        _edges[2]->place(QRect(r.left() - t, r.top(), t, r.height()));
        _edges[3]->place(QRect(r.right() + 1, r.top(), t, r.height()));
        _rect = r;
        placeLabel();
    }

    void hideRect()
    {
        for (EdgeStrip* e : _edges) e->hide();
    }

    void setOffset(int o)
    {
        for (EdgeStrip* e : _edges) e->setOffset(o);
    }

    void setText(const QString& l1, const QString& l2, bool accent)
    {
        _label->setText(l1, l2, accent);
        placeLabel();
    }

    void hideAll()
    {
        hideRect();
        _label->hide();
        _rect = QRect();
    }

private:
    void placeLabel()
    {
        if (_rect.isNull()) return;
        QScreen* s = QGuiApplication::screenAt(_rect.center());
        if (!s) s = QGuiApplication::primaryScreen();
        const QRect scr = s->geometry();
        const QSize sz  = _label->size();
        QPoint      p(_rect.left(), _rect.bottom() + 6);
        if (p.y() + sz.height() > scr.bottom())
            p.setY(_rect.top() - 6 - sz.height());
        if (p.y() < scr.top()) p.setY(_rect.top() + 6); // inside a full-screen target
        p.setX(std::clamp(p.x(), scr.left(), std::max(scr.left(), scr.right() - sz.width())));
        _label->place(QRect(p, sz));
    }

    EdgeStrip*   _edges[4];
    LabelWindow* _label;
    QRect        _rect;
};

// ---------------------------------------------------------------------------

DocCapture::Options
DocCapture::optionsFromEnvAndArgs(const QStringList& args)
{
    Options o;
    o.outDir = qEnvironmentVariable("ZODIAC_CAPTURE_DIR");
    bool         ok  = false;
    const double dpr = qEnvironmentVariable("ZODIAC_CAPTURE_DPR").toDouble(&ok);
    if (ok) o.dpr = dpr;

    for (const QString& a : args) {
        auto value = [&a](const char* key) { return a.mid(int(strlen(key))); };
        if (a.startsWith("--doc-capture-dir=", Qt::CaseInsensitive)) {
            o.outDir = value("--doc-capture-dir=");
        } else if (a.startsWith("--doc-capture-dpr=", Qt::CaseInsensitive)) {
            const double d = value("--doc-capture-dpr=").toDouble(&ok);
            if (ok) o.dpr = d;
        } else if (a.startsWith("--doc-capture-delay=", Qt::CaseInsensitive)) {
            const int ms = value("--doc-capture-delay=").toInt(&ok);
            if (ok) o.delayMs = ms;
        } else if (a.startsWith("--doc-capture-manifest=", Qt::CaseInsensitive)) {
            o.manifest = value("--doc-capture-manifest=");
        } else if (a.startsWith("--doc-capture-script=", Qt::CaseInsensitive)) {
            o.manifest  = value("--doc-capture-script=");
            o.runScript = true;
        } else if (a.startsWith("--doc-capture-img-dir=", Qt::CaseInsensitive)) {
            o.imgDir = value("--doc-capture-img-dir=");
        } else if (a.startsWith("--doc-capture-only=", Qt::CaseInsensitive)) {
            o.only = value("--doc-capture-only=");
        }
    }
    if (!o.manifest.isEmpty()) {
        o.manifest = QFileInfo(o.manifest).absoluteFilePath();
        if (o.imgDir.isEmpty())
            o.imgDir = QFileInfo(o.manifest).absolutePath() + "/docs/assets/img";
    }
    if (o.outDir.isEmpty())
        o.outDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) +
                   "/Zodiac captures";
    o.dpr     = std::clamp(o.dpr, 1.0, 8.0);
    o.delayMs = std::clamp(o.delayMs, 0, 30000);
    return o;
}

DocCapture*
DocCapture::installIfRequested(const QStringList& args)
{
    const bool requested =
        std::any_of(args.begin(), args.end(), [](const QString& a) {
            return a.compare("--doc-capture", Qt::CaseInsensitive) == 0 ||
                   a.startsWith("--doc-capture-", Qt::CaseInsensitive);
        });
    if (!requested) return nullptr;

    // Native dialogs (QFileDialog) are OS windows render() cannot draw.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    auto* dc = new DocCapture(optionsFromEnvAndArgs(args), qApp);
    qApp->installEventFilter(dc);
    if (!dc->_opt.manifest.isEmpty()) {
        dc->_script = new DocCaptureScript(dc);
        if (dc->_opt.runScript)
            dc->_script->startBatch();
        else
            dc->_script->load();
    }
    return dc;
}

DocCapture::DocCapture(const Options& opt, QObject* parent)
    : QObject(parent), _opt(opt)
{
    _outline = new CaptureOutline(this);
    _tick.setInterval(80);
    connect(&_tick, &QTimer::timeout, this, &DocCapture::tick);
    // Tear the overlay windows down while the widget system is still alive.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        _tick.stop();
        _active = false;
        delete _outline;
        _outline = nullptr;
    });
}

DocCapture::~DocCapture() = default;

bool
DocCapture::eventFilter(QObject* obj, QEvent* ev)
{
    switch (ev->type()) {
    case QEvent::KeyPress: {
        auto*      ke   = static_cast<QKeyEvent*>(ev);
        const auto mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier |
                                             Qt::AltModifier | Qt::MetaModifier);
        if (ke->key() == Qt::Key_F12 && mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (!ke->isAutoRepeat()) setActive(!_active);
            return true;
        }
        if (!_active) return false;
        switch (ke->key()) {
        case Qt::Key_F12:
            if (!ke->isAutoRepeat())
                startCapture(mods & Qt::AltModifier, mods & Qt::ShiftModifier);
            return true;
        case Qt::Key_F11:
            if (mods != Qt::NoModifier) return false;
            if (!ke->isAutoRepeat()) _frozen = !_frozen;
            return true;
        case Qt::Key_PageUp:
        case Qt::Key_PageDown: {
            const int dir = ke->key() == Qt::Key_PageUp ? +1 : -1;
            if (mods & Qt::ControlModifier) {
                if (_script) _script->stepEntry(-dir); // Ctrl+PgDn = next shot
                updateLabel();
            } else {
                step(dir);
            }
            return true;
        }
        case Qt::Key_F10:
            if (!_script || mods != Qt::NoModifier) return false;
            if (!ke->isAutoRepeat()) _script->runCurrentSetup();
            return true;
        case Qt::Key_Escape:
            if (QApplication::activePopupWidget()) return false; // closes the popup
            if (_pending)
                _pending = false;
            else
                setActive(false);
            return true;
        default: return false;
        }
    }
    case QEvent::Wheel: {
        if (!_active) return false;
        auto* we = static_cast<QWheelEvent*>(ev);
        if (we->modifiers() & Qt::ShiftModifier) return false; // scroll normally
        _wheelAccum += we->angleDelta().y();
        while (_wheelAccum >= 120) { step(+1); _wheelAccum -= 120; }
        while (_wheelAccum <= -120) { step(-1); _wheelAccum += 120; }
        return true;
    }
    default: break;
    }
    return QObject::eventFilter(obj, ev);
}

void
DocCapture::setActive(bool on)
{
    if (!_outline) return;
    _active     = on;
    _frozen     = false;
    _pending    = false;
    _wheelAccum = 0;
    if (on) {
        _lastPos = QPoint(INT_MIN, INT_MIN);
        _tick.start();
        toast(tr("Doc capture on — saving to %1").arg(QDir::toNativeSeparators(_opt.outDir)));
    } else {
        _tick.stop();
        _chain.clear();
        _outline->hideAll();
        toast(tr("Doc capture off"));
    }
}

QWidget*
DocCapture::target() const
{
    return _depth < _chain.size() ? _chain[_depth].data() : nullptr;
}

void
DocCapture::retarget(QWidget* leaf)
{
    if (!_chain.isEmpty() && _chain.first() == leaf) return;
    QWidget*              cur   = target();
    const QList<QWidget*> chain = ancestorChain(leaf);
    _chain.clear();
    for (QWidget* w : chain) _chain << w;
    // Once a container is selected, moving around inside it keeps it selected.
    const int keep = cur ? int(chain.indexOf(cur)) : -1;
    _depth         = keep >= 0 ? keep : 0;
    if (target() != cur) emit targetChanged(target());
}

void
DocCapture::step(int dir)
{
    if (_chain.isEmpty()) return;
    const int d = std::clamp(_depth + dir, 0, int(_chain.size()) - 1);
    if (d == _depth) return;
    _depth = d;
    emit targetChanged(target());
    updateLabel();
}

void
DocCapture::tick()
{
    if (!_active || !_outline) return;
    static int offset = 0;
    _outline->setOffset(++offset);

    if (!_frozen) {
        const QPoint pos = QCursor::pos();
        if (pos != _lastPos) {
            _lastPos = pos;
            if (QWidget* leaf = widgetAtGlobal(pos)) retarget(leaf);
        }
    }

    // Following the target each tick covers moves, resizes and dock changes.
    QWidget* t = target();
    if (t && t->isVisible())
        _outline->setRect(globalRect(t));
    else
        _outline->hideRect();

    if (_pending && _countdown.elapsed() >= _opt.delayMs) {
        _pending = false;
        if (capture(t, _pendingWithPopups, _pendingWithPopups ? "delayed+popups" : "delayed")) {
            // A tooltip shown during the countdown is saved as its own file.
            for (QWidget* tl : QApplication::topLevelWidgets()) {
                if (tl->isVisible() && tl->inherits("QTipLabel"))
                    capture(tl, false, "tooltip", fileStem(t) + "-tooltip");
            }
        }
    }
    updateLabel();
}

void
DocCapture::updateLabel()
{
    if (!_outline) return;
    QWidget* t = target();
    QString  line1;
    if (_pending) {
        const int left = (_opt.delayMs - int(_countdown.elapsed()) + 999) / 1000;
        line1          = tr("Capturing in %1…  (Esc cancels)").arg(std::max(left, 0));
    } else if (t) {
        const QSize px = t->size() * _opt.dpr;
        line1 = QString("%1  ·  %2×%3 → %4×%5 px  ·  level %6/%7%8")
                    .arg(label(t))
                    .arg(t->width()).arg(t->height())
                    .arg(px.width()).arg(px.height())
                    .arg(_depth + 1).arg(_chain.size())
                    .arg(_frozen ? "  [frozen]" : "");
    } else {
        line1 = tr("Point at a widget");
    }
    const bool showToast = _toastTimer.isValid() && _toastTimer.elapsed() < 2500;
    QString    line2     = showToast ? _toast : QString(HelpLine);
    if (_script && !showToast) line2 = _script->entryLine();
    _outline->setText(line1, line2, showToast);
}

void
DocCapture::startCapture(bool withPopups, bool delayed)
{
    if (delayed) {
        _pending           = true;
        _pendingWithPopups = withPopups;
        _countdown.start();
        return;
    }
    if (_script && _script->hasEntry()) {
        // Manifest pass: the shot goes under the entry's own file name, and
        // the widget picked is written back as its target.
        QWidget* w = target();
        if (!w) {
            toast(tr("Nothing selected"));
            return;
        }
        _script->captureEntry(w, withPopups);
        return;
    }
    capture(target(), withPopups, withPopups ? "popups" : "widget");
}

bool
DocCapture::capture(QWidget* w, bool withPopups, const QString& mode, const QString& stem)
{
    if (!w) {
        toast(tr("Nothing selected"));
        return false;
    }
    QRect        area;
    const QImage img = grab(w, withPopups, &area);
    if (img.isNull()) {
        toast(tr("Capture failed: empty image"));
        return false;
    }
    QDir().mkpath(_opt.outDir);
    const QString path = nextFreePath(stem.isEmpty() ? fileStem(w) : stem);
    if (const QString err = save(w, img, area, path, mode); !err.isEmpty()) {
        toast(err);
        return false;
    }
    toast(tr("Saved %1 (%2×%3)").arg(QFileInfo(path).fileName())
              .arg(img.width()).arg(img.height()));
    emit targetActivated(w);
    return true;
}

QImage
DocCapture::grab(QWidget* w, bool withPopups, QRect* area) const
{
    *area            = globalRect(w);
    const QImage src = withPopups ? renderWithPopups(w, area) : renderWidget(w);
    if (src.isNull()) return QImage();
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    // Record the scale so image viewers show it at its on-screen size.
    const int dpm = qRound(_opt.dpr * 96.0 / 0.0254);
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
    return img;
}

QString
DocCapture::save(QWidget* w, const QImage& img, const QRect& area,
                 const QString& path, const QString& mode) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!img.save(path, "PNG"))
        return tr("Could not write %1").arg(QDir::toNativeSeparators(path));
    Descriptor d = describe(w);
    d.globalGeom = area;
    appendLog(d, QFileInfo(path).fileName(), img, mode, HelpTopics::topicFor(w).page);
    return QString();
}

QImage
DocCapture::renderWidget(QWidget* w) const
{
    if (w->size().isEmpty()) return QImage();
    QImage img(w->size() * _opt.dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(_opt.dpr);
    img.fill(backgroundFor(w));

    QPainter p(&img);
    w->render(&p, QPoint(), QRegion(),
              QWidget::DrawWindowBackground | QWidget::DrawChildren);

    // render() leaves embedded native windows blank: the Planets slide is a
    // QQuickView inside createWindowContainer. Grab its scene graph instead;
    // it comes back at the screen's own DPR and is scaled to fit.
    // The container re-parents the QQuickView under the top-level's native
    // window, not under the container widget, so match by window and area.
    QWindow* top = w->window()->windowHandle();
    for (QWindow* win : QGuiApplication::allWindows()) {
        auto* qw = qobject_cast<QQuickWindow*>(win);
        if (!qw || !qw->isExposed() || !qw->isVisible()) continue;
        QWindow* root = qw;
        while (root->parent()) root = root->parent();
        if (root != top) continue;
        const QRect r(w->mapFromGlobal(qw->mapToGlobal(QPoint(0, 0))), qw->size());
        if (!r.intersects(w->rect())) continue;
        p.save();
        p.setClipRect(w->rect());
        p.drawImage(r, qw->grabWindow());
        p.restore();
    }
    return img;
}

QImage
DocCapture::renderWithPopups(QWidget* w, QRect* area) const
{
    // Frame the shot on the nearest selection outside any popup -- for a
    // combo's open list that is the combo itself -- plus every open popup.
    QWidget* base = w;
    while (base && base->window()->windowType() == Qt::Popup)
        base = base->window()->parentWidget();
    if (!base) base = w;
    QWidget* win = base->window();

    QRect           r = globalRect(base);
    QList<QWidget*> pops;
    for (QWidget* tl : QApplication::topLevelWidgets()) {
        if (tl == win || !tl->isVisible() || isOverlay(tl)) continue;
        const Qt::WindowType t = tl->windowType();
        if (t != Qt::Popup && t != Qt::ToolTip) continue;
        pops << tl;
        r |= globalRect(tl);
    }
    // Tooltips sit above menus and dropdowns.
    std::stable_sort(pops.begin(), pops.end(), [](QWidget* a, QWidget* b) {
        return a->windowType() != Qt::ToolTip && b->windowType() == Qt::ToolTip;
    });

    QImage img(r.size() * _opt.dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(_opt.dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.translate(-r.topLeft());
    p.drawImage(globalRect(win), renderWidget(win));
    for (QWidget* pop : pops) p.drawImage(globalRect(pop), renderWidget(pop));
    *area = r;
    return img;
}

QString
DocCapture::nextFreePath(const QString& stem) const
{
    const QDir dir(_opt.outDir);
    for (int n = 1;; ++n) {
        const QString f = QString("%1-%2.png").arg(stem).arg(n, 3, 10, QChar('0'));
        if (!dir.exists(f)) return dir.filePath(f);
    }
}

void
DocCapture::appendLog(const Descriptor& d,
                      const QString&    file,
                      const QImage&     img,
                      const QString&    mode,
                      const QString&    helpPage) const
{
    QDir().mkpath(_opt.outDir);
    QFile log(QDir(_opt.outDir).filePath("captures.jsonl"));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    const QJsonObject o{
        { "time", QDateTime::currentDateTime().toString(Qt::ISODate) },
        { "file", file },
        { "objectName", d.objectName },
        { "className", d.className },
        { "id", d.id },
        { "title", d.title },
        { "geometry", QJsonArray{ d.globalGeom.x(), d.globalGeom.y(),
                                  d.globalGeom.width(), d.globalGeom.height() } },
        { "pixels", QJsonArray{ img.width(), img.height() } },
        { "dpr", _opt.dpr },
        { "mode", mode },
        { "helpPage", helpPage }, // F1 topic of the widget (HelpTopics)
    };
    log.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
}

void
DocCapture::toast(const QString& msg)
{
    _toast = msg;
    _toastTimer.start();
    for (QWidget* tl : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<QMainWindow*>(tl); mw && mw->isVisible())
            mw->statusBar()->showMessage(msg, 4000);
    }
    if (_active) updateLabel();
}
