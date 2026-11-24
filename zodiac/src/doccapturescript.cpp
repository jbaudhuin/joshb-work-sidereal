#include "doccapturescript.h"
#include "doccapture.h"
#include "mainwindow.h"
#include "thememanager.h"
#include "widgetpicker.h"
#include "../details/src/transits.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>
#include <QSet>
#include <QToolButton>
#include <algorithm>
#include <cstdio>

using namespace WidgetPicker;

namespace
{

constexpr int StepSettleMs = 300;  // after each non-waiting step
constexpr int ShotSettleMs = 700;  // after a shot's setup, before the grab
constexpr int PollMs       = 100;

MainWindow*
mainWindow()
{
    for (QWidget* tl : QApplication::topLevelWidgets())
        if (auto* mw = qobject_cast<MainWindow*>(tl)) return mw;
    return nullptr;
}

// Labels as the user reads them: no mnemonic, no trailing ellipsis.
QString
plain(QString s)
{
    s.remove('&');
    s = s.trimmed();
    if (s.endsWith("...")) s.chop(3);
    if (s.endsWith(QChar(0x2026))) s.chop(1);
    return s.trimmed();
}

bool
sameLabel(const QString& a, const QString& b)
{
    return plain(a).compare(plain(b), Qt::CaseInsensitive) == 0;
}

QString
widgetKey(const QJsonValue& arg)
{
    return arg.isObject() ? arg.toObject().value("widget").toString() : arg.toString();
}

QAction*
findAction(const QString& key)
{
    QList<QAction*> all;
    for (QWidget* tl : QApplication::topLevelWidgets()) {
        if (isOverlay(tl)) continue;
        all += tl->findChildren<QAction*>();
        all += tl->actions();
        for (QWidget* c : tl->findChildren<QWidget*>()) all += c->actions();
    }
    for (QAction* a : all)
        if (a->objectName() == key) return a;

    // By text, labels repeat: the "Settings" toolbar's show/hide action is
    // also called "Settings", and the Tables view has its own "Planets"
    // toggle. Toolbar/dock toggles never count, and an action with a visible
    // button beats one whose widgets are hidden.
    QSet<QAction*> toggles;
    for (QWidget* tl : QApplication::topLevelWidgets()) {
        for (auto* tb : tl->findChildren<QToolBar*>()) toggles << tb->toggleViewAction();
        for (auto* d : tl->findChildren<QDockWidget*>()) toggles << d->toggleViewAction();
    }
    auto shown = [](const QAction* a) {
        const auto objs = a->associatedObjects();
        return std::any_of(objs.begin(), objs.end(), [](QObject* o) {
            auto* w = qobject_cast<QWidget*>(o);
            return w && w->isVisible() && !qobject_cast<QMenu*>(w);
        });
    };
    QAction* best      = nullptr;
    int      bestScore = -1;
    for (QAction* a : all) {
        if (toggles.contains(a) || !sameLabel(a->text(), key)) continue;
        const int score = (a->isEnabled() ? 2 : 0) + (shown(a) ? 1 : 0);
        if (score > bestScore) {
            best      = a;
            bestScore = score;
        }
    }
    return best;
}

void
sendClick(QWidget* w, const QPoint& p, bool dbl)
{
    const QPointF lp(p), gp(w->mapToGlobal(p));
    QMouseEvent press(QEvent::MouseButtonPress, lp, gp, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, lp, gp, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
    if (dbl) {
        QMouseEvent dc(QEvent::MouseButtonDblClick, lp, gp, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release2(QEvent::MouseButtonRelease, lp, gp, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(w, &dc);
        QApplication::sendEvent(w, &release2);
    }
}

// The row a step names, by "row" number or by "text" (matched anywhere in
// the given column, tree children included).
QModelIndex
findRow(QAbstractItemView* view, const QJsonObject& o, QString* err)
{
    QAbstractItemModel* m = view->model();
    if (!m) {
        *err = "view has no model";
        return {};
    }
    const int col = o.value("column").toInt(0);
    QModelIndex idx;
    if (o.contains("text")) {
        const QModelIndexList hits =
            m->match(m->index(0, col), Qt::DisplayRole, o.value("text").toString(), 1,
                     Qt::MatchContains | Qt::MatchRecursive | Qt::MatchWrap);
        if (!hits.isEmpty()) idx = hits.first();
    } else {
        idx = m->index(o.value("row").toInt(0), col);
    }
    if (!idx.isValid()) *err = "no such row";
    return idx;
}

void
closePopupsAndDialogs()
{
    for (int guard = 0; guard < 20; ++guard) {
        if (QWidget* p = QApplication::activePopupWidget()) {
            p->close();
        } else if (QWidget* m = QApplication::activeModalWidget()) {
            if (auto* d = qobject_cast<QDialog*>(m))
                d->reject();
            else
                m->close();
        } else {
            break;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------

DocCaptureScript::DocCaptureScript(DocCapture* dc) : QObject(dc), _dc(dc)
{
    _poll.setInterval(PollMs);
}

bool
DocCaptureScript::load()
{
    _manifest = _dc->_opt.manifest;
    QFile f(_manifest);
    if (!f.open(QIODevice::ReadOnly)) {
        _dc->toast(tr("Cannot read manifest %1").arg(QDir::toNativeSeparators(_manifest)));
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        _dc->toast(tr("Manifest parse error: %1").arg(pe.errorString()));
        return false;
    }
    _root     = doc.object();
    _defaults = _root.value("defaults").toObject();
    _shots    = _root.value("shots").toArray();
    _current  = _shots.isEmpty() ? -1 : 0;

    if (!_dc->_opt.runScript) {
        // The interactive pass starts picking straight away.
        QTimer::singleShot(1500, _dc, [this]() {
            if (!_dc->_active) _dc->setActive(true);
            if (!userDataDirOverridden())
                _dc->toast(tr("No --data-dir: personal charts and sessions may show"));
        });
    }
    return true;
}

QString
DocCaptureScript::imagePath(const QJsonObject& s) const
{
    return QDir(_dc->_opt.imgDir).filePath(s.value("file").toString());
}

bool
DocCaptureScript::writeBack(int i, const QJsonObject& s)
{
    _shots[i]       = s;
    _root["shots"]  = _shots;
    QFile f(_manifest);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(_root).toJson(QJsonDocument::Indented));
    return true;
}

// --------------------------- interactive pass -------------------------------

void
DocCaptureScript::stepEntry(int dir)
{
    if (_shots.isEmpty()) return;
    _current = std::clamp(_current + dir, 0, int(_shots.size()) - 1);
}

QString
DocCaptureScript::entryLine() const
{
    if (!hasEntry()) return tr("Manifest is empty");
    const QJsonObject s = shot(_current);
    QString caption     = s.value("caption").toString();
    if (caption.size() > 70) caption = caption.left(69) + QChar(0x2026);
    QString flags;
    if (s.value("mode").toString() == "manual") flags += tr("  [manual]");
    if (!s.value("target").toString().isEmpty()) flags += tr("  [target set]");
    return tr("[%1/%2] %3%4 — %5   ·  Ctrl+PgUp/PgDn shot · F10 setup · F12 save")
        .arg(_current + 1)
        .arg(_shots.size())
        .arg(s.value("file").toString(), flags, caption);
}

void
DocCaptureScript::captureEntry(QWidget* w, bool withPopups)
{
    QJsonObject   s    = shot(_current);
    const QString path = imagePath(s);
    QRect         area;
    const QImage  img = _dc->grab(w, withPopups, &area);
    if (img.isNull()) {
        _dc->toast(tr("Capture failed: empty image"));
        return;
    }
    if (const QString err = _dc->save(w, img, area, path, "manifest"); !err.isEmpty()) {
        _dc->toast(err);
        return;
    }

    // Record what was picked. A popup composite is framed on the nearest
    // widget outside the popup, so that is what a script run must find.
    QWidget* base = w;
    while (withPopups && base && base->window()->windowType() == Qt::Popup)
        base = base->window()->parentWidget();
    if (!base) base = w;
    s["target"] = base == mainWindow() ? QStringLiteral("window") : stableId(base);
    if (withPopups)
        s["popups"] = true;
    else
        s.remove("popups");
    const bool wrote = writeBack(_current, s);
    _dc->toast(tr("Saved %1 (%2×%3)%4")
                   .arg(s.value("file").toString())
                   .arg(img.width())
                   .arg(img.height())
                   .arg(wrote ? tr(", target recorded") : tr(" — manifest NOT updated")));
}

void
DocCaptureScript::runCurrentSetup()
{
    if (!hasEntry()) return;
    if (_running) {
        _dc->toast(tr("Setup still running"));
        return;
    }
    const QJsonArray steps = shot(_current).value("setup").toArray();
    _dc->toast(tr("Running %n setup step(s)…", nullptr, int(steps.size())));
    runSteps(steps, [this](const QString& err) {
        _dc->toast(err.isEmpty() ? tr("Setup done") : tr("Setup failed: %1").arg(err));
    });
}

// ------------------------------ step engine ---------------------------------

void
DocCaptureScript::runSteps(const QJsonArray& steps, Done done)
{
    _steps     = steps;
    _stepIndex = 0;
    _stepError.clear();
    _stepsDone = std::move(done);
    _running   = true;
    QTimer::singleShot(0, this, &DocCaptureScript::nextStep);
}

void
DocCaptureScript::finishSteps(const QString& error)
{
    _running = false;
    _poll.stop();
    _poll.disconnect();
    Done done = std::move(_stepsDone);
    _stepsDone = nullptr;
    if (done) done(error);
}

void
DocCaptureScript::nextStep()
{
    if (!_running) return;
    if (!_stepError.isEmpty()) return finishSteps(_stepError);
    if (_stepIndex >= _steps.size()) return finishSteps(QString());

    const int         n    = _stepIndex++;
    const QJsonObject step = _steps.at(n).toObject();
    if (step.size() != 1) {
        _stepError = tr("step %1: expected one {action: argument}").arg(n + 1);
        return nextStep();
    }
    const QString action = step.constBegin().key();
    // Queue the continuation first: a step that opens a modal dialog or menu
    // blocks in exec(), and the script carries on inside that nested loop.
    // Waiting steps schedule their own continuation instead.
    static const QStringList waits = { "wait", "waitForFinders", "waitForWidget" };
    const bool               async = waits.contains(action);
    if (!async) QTimer::singleShot(StepSettleMs, this, &DocCaptureScript::nextStep);

    const QString err = execStep(action, step.constBegin().value());
    if (!err.isEmpty()) {
        _stepError = tr("step %1 (%2): %3").arg(n + 1).arg(action, err);
        if (async) nextStep(); // nothing else is queued
    }
}

void
DocCaptureScript::waitUntil(std::function<bool()> ready, int timeoutMs, const QString& what)
{
    _waitClock.start();
    _poll.disconnect();
    // The condition must hold for a few polls in a row: finders start a beat
    // after the change that triggers them.
    auto stable = std::make_shared<int>(0);
    connect(&_poll, &QTimer::timeout, this, [this, ready, timeoutMs, what, stable]() {
        *stable = ready() ? *stable + 1 : 0;
        if (*stable >= 3) {
            _poll.stop();
            _poll.disconnect();
            nextStep();
        } else if (_waitClock.elapsed() > timeoutMs) {
            _poll.stop();
            _poll.disconnect();
            _stepError = tr("timed out waiting for %1").arg(what);
            nextStep();
        }
    });
    _poll.start();
}

QString
DocCaptureScript::execStep(const QString& action, const QJsonValue& arg)
{
    MainWindow* mw = mainWindow();
    if (!mw) return "no main window";
    const QJsonObject o = arg.toObject();

    auto widgetArg = [&](QString* err) -> QWidget* {
        const QString key = widgetKey(arg);
        QWidget*      w   = key.isEmpty() ? nullptr : resolve(key);
        if (!w) *err = QString("widget '%1' not found").arg(key);
        return w;
    };

    if (action == "wait") {
        const int ms = arg.toInt(500);
        waitUntil([this, ms]() { return _waitClock.elapsed() >= ms; }, ms + 5000, "wait");
        return {};
    }
    if (action == "waitForFinders") {
        const int timeout = arg.isDouble() ? arg.toInt() : 180000;
        waitUntil([mw]() {
            const auto all = mw->findChildren<Transits*>();
            return std::none_of(all.begin(), all.end(),
                                [](const Transits* t) { return t->findersBusy(); });
        }, timeout, "event finders");
        return {};
    }
    if (action == "waitForWidget") {
        const QString key = widgetKey(arg);
        waitUntil([key]() {
            QWidget* w = resolve(key);
            return w && w->isVisible();
        }, o.value("timeout").toInt(10000), key);
        return {};
    }

    if (action == "loadChart") {
        const QString spec  = arg.isObject() ? o.value("chart").toString() : arg.toString();
        const QString label = spec.section('/', 0, 0);
        const QString name  = spec.section('/', 1);
        QString       dir   = AstroFile::fixedChartDirMap().value(label);
        if (dir.isEmpty() || name.isEmpty())
            return QString("'%1' is not <%2>/<name>")
                .arg(spec, AstroFile::fixedChartDirMapKeys().join('|'));
        if (QDir::isRelativePath(dir) && !QDir(dir).exists())
            dir = QCoreApplication::applicationDirPath() + "/" + dir;
        const AFileInfo fi(QDir(dir), name);
        if (!fi.exists()) return QString("no chart file %1").arg(fi.absoluteFilePath());
        auto* fb = mw->findChild<FilesBar*>();
        if (!fb) return "no chart tab bar";
        if (o.value("second").toBool()) {
            fb->openFileAsSecond(fi);
        } else if (o.value("newTab").toBool()) {
            fb->openFileInNewTab(fi);
        } else {
            // As dropping a chart on the view: replaces the tab's charts.
            fb->closeSecondaryChart();
            fb->openFile(fi);
            fb->updateTab(fb->currentIndex());
        }
        return {};
    }
    if (action == "triggerAction") {
        QAction* a = findAction(arg.toString());
        if (!a) return QString("action '%1' not found").arg(arg.toString());
        if (!a->isEnabled()) return QString("action '%1' is disabled").arg(arg.toString());
        a->trigger(); // may block in exec(); the next step is already queued
        return {};
    }
    if (action == "click") {
        QString  err;
        QWidget* w = widgetArg(&err);
        if (!w) return err;
        auto* b = qobject_cast<QAbstractButton*>(w);
        if (o.contains("text")) {
            b = nullptr;
            const QString text = o.value("text").toString();
            for (QAbstractButton* c : w->findChildren<QAbstractButton*>()) {
                if (!c->isVisible()) continue;
                if (sameLabel(c->text(), text) ||
                    sameLabel(c->toolTip().section('\n', 0, 0), text)) {
                    b = c;
                    break;
                }
            }
            if (!b) return QString("no button '%1' in %2").arg(text, widgetKey(arg));
        }
        if (!b) return QString("%1 is not a button").arg(widgetKey(arg));
        if (o.contains("checked") && b->isCheckable() &&
            b->isChecked() == o.value("checked").toBool())
            return {}; // already in the wanted state
        b->click();
        return {};
    }
    if (action == "setTab") {
        const QString   tab = o.value("tab").toString();
        QList<QTabBar*> bars;
        if (o.contains("widget")) {
            QString  err;
            QWidget* w = widgetArg(&err);
            if (!w) return err;
            if (auto* tw = qobject_cast<QTabWidget*>(w)) bars << tw->tabBar();
            if (auto* tb = qobject_cast<QTabBar*>(w)) bars << tb;
            bars += w->findChildren<QTabBar*>();
        } else {
            for (QWidget* tl : QApplication::topLevelWidgets())
                if (tl->isVisible()) bars += tl->findChildren<QTabBar*>();
        }
        for (QTabBar* bar : bars) {
            for (int i = 0; i < bar->count(); ++i) {
                if (!sameLabel(bar->tabText(i), tab)) continue;
                bar->setCurrentIndex(i);
                return {};
            }
        }
        return QString("no tab '%1'").arg(tab);
    }
    if (action == "setCombo" || action == "setHarmonic") {
        QComboBox* cb = nullptr;
        QString    text;
        if (action == "setHarmonic") {
            cb   = qobject_cast<QComboBox*>(resolve("harmonicSelector"));
            text = QString::number(arg.toInt());
        } else {
            QString err;
            QWidget* w = widgetArg(&err);
            text       = o.value("text").toString();
            cb         = qobject_cast<QComboBox*>(w);
            if (!cb && w) {
                // A container: the first visible combo holding that item.
                for (auto* c : w->findChildren<QComboBox*>())
                    if (c->isVisible() && c->findText(text, Qt::MatchContains) >= 0) { cb = c; break; }
            }
            if (!cb) return err.isEmpty() ? QString("%1 is not a combo box").arg(widgetKey(arg)) : err;
        }
        if (!cb) return "harmonicSelector not found";
        int i = o.contains("index") ? o.value("index").toInt() : cb->findText(text, Qt::MatchFixedString);
        if (i < 0) i = cb->findText(text, Qt::MatchContains);
        if (i < 0 || i >= cb->count()) return QString("no item '%1'").arg(text);
        cb->setCurrentIndex(i);
        emit cb->activated(i); // as if the user picked it
        emit cb->textActivated(cb->itemText(i));
        return {};
    }
    if (action == "setText") {
        QString  err;
        QWidget* w  = widgetArg(&err);
        auto*    le = qobject_cast<QLineEdit*>(w);
        if (auto* cb = qobject_cast<QComboBox*>(w)) le = cb->lineEdit();
        if (!le) return w ? QString("%1 has no text field").arg(widgetKey(arg)) : err;
        le->setFocus();
        le->setText(o.value("text").toString());
        emit le->textEdited(le->text());
        if (o.value("submit").toBool()) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(le, &press);
            QApplication::sendEvent(le, &release);
        }
        return {};
    }
    if (action == "setEventRange") {
        auto*       t    = mw->findChild<Transits*>();
        const QDate from = QDate::fromString(o.value("from").toString(), Qt::ISODate);
        const QDate to   = QDate::fromString(o.value("to").toString(), Qt::ISODate);
        if (!t) return "no Events panel";
        if (!from.isValid() || !to.isValid() || to < from) return "bad from/to dates";
        t->setDateRange(from, to);
        return {};
    }
    if (action == "selectRow" || action == "contextMenu") {
        QString  err;
        QWidget* w = widgetArg(&err);
        if (!w) return err;
        QWidget* target = w;
        QPoint   p      = w->rect().center();
        // A dock or panel names the view inside it.
        auto* view = qobject_cast<QAbstractItemView*>(w);
        if (!view && action == "selectRow") {
            for (auto* v : w->findChildren<QAbstractItemView*>())
                if (v->isVisible()) { view = v; break; }
        }
        if (view) {
            target = view->viewport();
            p      = target->rect().center();
            if (action == "selectRow" || o.contains("row") || o.contains("text")) {
                const QModelIndex idx = findRow(view, o, &err);
                if (!idx.isValid()) return err;
                view->scrollTo(idx);
                p = view->visualRect(idx).center();
                if (o.value("expand").toBool()) {
                    if (auto* tree = qobject_cast<QTreeView*>(view)) tree->expand(idx);
                    return {}; // expanding needs no click
                }
            }
        } else if (action == "selectRow") {
            return QString("%1 is not a list, table or tree").arg(widgetKey(arg));
        }
        if (action == "selectRow") {
            sendClick(target, p, o.value("double").toBool());
        } else {
            QContextMenuEvent ev(QContextMenuEvent::Mouse, p, target->mapToGlobal(p));
            QApplication::sendEvent(target, &ev); // menus block in exec()
        }
        return {};
    }
    if (action == "openPopup") {
        QString  err;
        QWidget* w = widgetArg(&err);
        if (!w) return err;
        if (auto* cb = qobject_cast<QComboBox*>(w))
            cb->showPopup();
        else if (auto* tb = qobject_cast<QToolButton*>(w))
            tb->showMenu(); // blocks
        else if (auto* pb = qobject_cast<QPushButton*>(w); pb && pb->menu())
            pb->showMenu(); // blocks
        else
            return QString("%1 has no popup").arg(widgetKey(arg));
        return {};
    }
    if (action == "hover") {
        QString  err;
        QWidget* w = widgetArg(&err);
        if (!w) return err;
        QCursor::setPos(w->mapToGlobal(w->rect().center()));
        return {};
    }
    if (action == "showDock") {
        auto* d = qobject_cast<QDockWidget*>(resolve(arg.toString()));
        if (!d) return QString("no dock '%1'").arg(arg.toString());
        d->show();
        d->raise();
        return {};
    }
    if (action == "setTheme") {
        _theme = arg.toString();
        ThemeManager::instance().setTheme(_theme, true);
        ThemeManager::instance().propagateThemeProperty(mw);
        return {};
    }
    if (action == "resizeWindow") {
        const QJsonArray sz = arg.toArray();
        if (sz.size() != 2) return "expected [width, height]";
        mw->showNormal();
        mw->resize(sz[0].toInt(), sz[1].toInt());
        if (QScreen* scr = mw->screen()) mw->move(scr->availableGeometry().topLeft() + QPoint(24, 24));
        return {};
    }
    if (action == "closeWindow") {
        QString  err;
        QWidget* w = widgetArg(&err);
        if (!w) return err;
        if (w->window() == mw) return "refusing to close the main window";
        w->window()->close();
        return {};
    }
    if (action == "closePopups" || action == "closeDialogs") {
        closePopupsAndDialogs();
        return {};
    }
    return QString("unknown action");
}

// --------------------------------- batch ------------------------------------

void
DocCaptureScript::startBatch()
{
    _batch = true;
    if (!load()) {
        report("(manifest)", "failed", tr("cannot load %1").arg(_dc->_opt.manifest));
        return finishBatch();
    }
    if (!userDataDirOverridden()) {
        report("(manifest)", "failed",
               "refusing to run without --data-dir: use a clean data folder so no "
               "personal charts or sessions appear in the shots");
        return finishBatch();
    }
    // Icons and the theme's stylesheet images load relative to the working
    // directory; from anywhere else the shots come out without them.
    if (QDir(QDir::currentPath()) != QDir(QCoreApplication::applicationDirPath())) {
        report("(manifest)", "failed",
               "run from the folder holding zodiac.exe (bin/): icons and theme "
               "images load relative to the working directory");
        return finishBatch();
    }

    // Wait for the window, then put it in the manifest's default state.
    QJsonArray init;
    if (_defaults.contains("windowSize"))
        init.append(QJsonObject{ { "resizeWindow", _defaults.value("windowSize") } });
    init.append(QJsonObject{ { "setTheme", _defaults.value("theme").toString("dark") } });
    if (_defaults.contains("chart"))
        init.append(QJsonObject{ { "loadChart", _defaults.value("chart") } });
    // Session-wide state every shot assumes (aspect set, ...).
    for (const QJsonValue& v : _defaults.value("setup").toArray()) init.append(v);
    init.append(QJsonObject{ { "waitForFinders", true } });

    _waitClock.start();
    _poll.disconnect();
    connect(&_poll, &QTimer::timeout, this, [this, init]() {
        MainWindow* mw = mainWindow();
        if (!mw || !mw->isVisible() || _waitClock.elapsed() < 1500) return;
        _poll.stop();
        _poll.disconnect();
        runSteps(init, [this](const QString& err) {
            if (!err.isEmpty()) {
                report("(defaults)", "failed", err);
                return finishBatch();
            }
            runNextShot();
        });
    });
    _poll.start();
}

void
DocCaptureScript::runNextShot()
{
    const QRegularExpression only(_dc->_opt.only);
    while (++_batchPos < _shots.size()) {
        const QJsonObject s    = shot(_batchPos);
        const QString     file = s.value("file").toString();
        if (!_dc->_opt.only.isEmpty() && !only.match(file).hasMatch()) continue;
        if (s.value("mode").toString() == "manual") {
            report(file, "skipped", "manual");
            continue;
        }
        if (s.value("target").toString().isEmpty()) {
            report(file, "skipped", "no target yet");
            continue;
        }
        break;
    }
    if (_batchPos >= _shots.size()) return finishBatch();

    const QJsonObject s = shot(_batchPos);
    QJsonArray        steps;
    const QString     theme = s.value("theme").toString(_defaults.value("theme").toString("dark"));
    if (theme != _theme) steps.append(QJsonObject{ { "setTheme", theme } });
    if (s.contains("windowSize"))
        steps.append(QJsonObject{ { "resizeWindow", s.value("windowSize") } });
    for (const QJsonValue& v : s.value("setup").toArray()) steps.append(v);

    _shotSteps = steps;
    _retried   = false;
    runShotSteps(s);
}

void
DocCaptureScript::runShotSteps(const QJsonObject& s)
{
    runSteps(_shotSteps, [this, s](const QString& err) {
        if (!err.isEmpty()) {
            report(s.value("file").toString(), "failed", err);
            cleanupAfterShot();
            return runNextShot();
        }
        const int settle = s.value("settle").toInt(_defaults.value("settle").toInt(ShotSettleMs));
        QTimer::singleShot(settle, this, [this, s]() { captureShot(s); });
    });
}

void
DocCaptureScript::captureShot(const QJsonObject& s)
{
    const QString file   = s.value("file").toString();
    const QString target = s.value("target").toString();
    QWidget*      w      = nullptr;
    if (target == "window")
        w = mainWindow();
    else if (target == "dialog")
        w = QApplication::activeModalWidget();
    else
        w = resolve(target);

    // A dropdown can close again before the grab (focus moving under it);
    // set the shot up once more rather than save it without its popup.
    const bool popupMissing = s.value("popups").toBool() && !QApplication::activePopupWidget();
    if (popupMissing && !_retried) {
        _retried = true;
        cleanupAfterShot();
        runShotSteps(s);
        return;
    }

    if (!w || !w->isVisible()) {
        report(file, "failed", QString("target '%1' %2").arg(target, w ? "not visible" : "not found"));
    } else if (popupMissing) {
        report(file, "failed", "popup closed before the grab (twice)");
    } else {
        QRect        area;
        const QImage img = _dc->grab(w, s.value("popups").toBool(), &area);
        const QString err =
            img.isNull() ? QString("empty image")
                         : _dc->save(w, img, area, imagePath(s), "script");
        if (err.isEmpty())
            report(file, "ok", QString("%1x%2").arg(img.width()).arg(img.height()));
        else
            report(file, "failed", err);
    }
    cleanupAfterShot();

    // teardown: undo what the setup changed that later shots shouldn't
    // inherit (a harmonic, an editor window). Its failures are reported but
    // don't fail the shot.
    const QJsonArray teardown = s.value("teardown").toArray();
    if (teardown.isEmpty()) {
        QTimer::singleShot(0, this, &DocCaptureScript::runNextShot);
        return;
    }
    runSteps(teardown, [this, file](const QString& err) {
        if (!err.isEmpty()) report(file, "warning", "teardown: " + err);
        runNextShot();
    });
}

void
DocCaptureScript::cleanupAfterShot()
{
    // Each shot starts from the main window: whatever its setup opened goes.
    closePopupsAndDialogs();
}

void
DocCaptureScript::report(const QString& file, const QString& status, const QString& detail)
{
    if (status == "failed") ++_failures;
    _results.append(QJsonObject{ { "file", file }, { "status", status }, { "detail", detail } });
    const QByteArray line = QString("[doc-capture] %1  %2  %3\n").arg(status, -7).arg(file, detail).toUtf8();
    fputs(line.constData(), stderr);
    fflush(stderr);
}

void
DocCaptureScript::finishBatch()
{
    int ok = 0, skipped = 0;
    for (const QJsonValue& v : _results) {
        const QString st = v.toObject().value("status").toString();
        ok += st == "ok";
        skipped += st == "skipped";
    }
    const QJsonObject summary{
        { "manifest", _manifest },
        { "imgDir", _dc->_opt.imgDir },
        { "ok", ok },
        { "skipped", skipped },
        { "failed", _failures },
        { "results", _results },
    };
    QDir().mkpath(_dc->_opt.outDir);
    QFile f(QDir(_dc->_opt.outDir).filePath("script-report.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    const QByteArray line = QString("[doc-capture] done: %1 ok, %2 skipped, %3 failed — %4\n")
                                .arg(ok).arg(skipped).arg(_failures)
                                .arg(QDir::toNativeSeparators(f.fileName()))
                                .toUtf8();
    fputs(line.constData(), stderr);
    fflush(stderr);

    const int code = _failures ? 1 : 0;
    QTimer::singleShot(0, qApp, [code]() {
        closePopupsAndDialogs();
        QCoreApplication::exit(code);
    });
}
