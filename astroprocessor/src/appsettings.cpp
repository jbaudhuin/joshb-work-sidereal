/****************************************************************************
**
** Copyright (C) 2010 Artem Vasilev.
** Contact: atten@coolline.ru
**
** This file is part of the FreeGate.
**
** FreeGate is free software: you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** FreeGate is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with FreeGate.  If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/

#include <QObject>
#include <QStringList>
#include <QSettings>
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QTabWidget>
#include <QFormLayout>
#include <QTextCodec>
#include <QTimer>
#include <QDebug>

#include "appsettings.h"

namespace {
int lastTab = -1;
}

/* =========================== APP SETTINGS ========================================= */

AppSettings::AppSettings()
{
}

AppSettings::AppSettings(const QVariantMap& map)
{
    _values = map;
}

void AppSettings::setValue(const QString& name, const QVariant& value)
{
    _values[name] = value;
}

void AppSettings::setValues(const AppSettings& s)
{
    QMapIterator<QString, QVariant> i(s.values());
    while (i.hasNext()) {
        i.next();
        setValue(i.key(), i.value());
    }
}

const QVariant AppSettings::value(const QString& name, const QVariant& defaultValue) const
{
    return _values.value(name, defaultValue);
}

void AppSettings::load(const QString& fileName)
{
    QSettings file(fileName, QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    file.setIniCodec(QTextCodec::codecForName("UTF-8"));
#endif

    foreach(const QString& key, _values.keys())
        setValue(key, file.value(key, value(key)));

    qDebug() << "AppSettings: loaded from" << fileName;
}

void AppSettings::save(const QString& fileName)
{
    QSettings file(fileName, QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    file.setIniCodec(QTextCodec::codecForName("UTF-8"));
#endif

    for (const QString& key : _values.keys()) {
        file.setValue(key, value(key));
    }

    qDebug() << "AppSettings: saved to" << fileName;
}

/* =========================== APP SETTINGS EDITOR ================================== */

AppSettingsEditor::AppSettingsEditor() : QDialog()
{
    changed = false;

    tabs = new QTabWidget(this);
    ok = new QPushButton(tr("OK"), this);
    cancel = new QPushButton(tr("Cancel"), this);
    bApply = new QPushButton(tr("Apply"), this);
    setDefault = new QPushButton(tr("By default"), this);

    setWindowTitle(tr("Settings"));
    bApply->setEnabled(false);

    QHBoxLayout* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(setDefault);
    btnLayout->addStretch(0);
    btnLayout->addSpacing(10);
    btnLayout->addWidget(ok);
    btnLayout->addWidget(cancel);
    btnLayout->addWidget(bApply);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addLayout(btnLayout);

    connect(bApply, SIGNAL(clicked()), this, SLOT(applySettings()));
    connect(ok, SIGNAL(clicked()), this, SLOT(applySettings()));
    connect(ok, SIGNAL(clicked()), this, SLOT(accept()));
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(setDefault, SIGNAL(clicked()), this, SLOT(restoreDefaults()));

    if (lastTab != -1) {
        // restore prior tab, after the dialog is assembled.
        QTimer::singleShot(0, [this] {
            tabs->setCurrentIndex(lastTab);
        });
    }
}

AppSettingsEditor::~AppSettingsEditor()
{
    lastTab = tabs->currentIndex(); // keep tabs on the tab
    foreach(QWidget* wdg, customWidgets) {
        // deattach another widgets before deleting
        wdg->setParent(this->parentWidget());
    }
}


void AppSettingsEditor::setObject(Customizable* obj)
{
    customObj = obj;
    settings = obj->currentSettings();
    defaultSettings = obj->defaultSettings();
}

void AppSettingsEditor::change()
{
    changed = true;
    bApply->setEnabled(true);
}

void AppSettingsEditor::addTab(const QString& tabName)
{
    QWidget* w = new QWidget;
    w->setLayout(new QFormLayout);
    tabs->addTab(w, tabName);
}

/*void AppSettingsEditor :: beginGroup  ( QString groupName )
 {
  if (!tabs->count()) addTab(tr("General"));

  // ...
 }*/

QFormLayout* AppSettingsEditor::lastLayout()
{
    if (!tabs->count()) addTab(tr("General"));
    int last = tabs->count() - 1;
    return (QFormLayout*)tabs->widget(last)->layout();
}

QWidget* 
AppSettingsEditor::addControl(const QString& valueName,
                              const QString& label)
{
    if (valueName.isEmpty()) return nullptr;

    QVariant ds = defaultSettings.value(valueName);
    if (ds.type() == QVariant::Int) {
        return addLineEdit(valueName, label);
    } else if (ds.type() == QVariant::Bool) {
        return addCheckBox(valueName, label);
    } else if (ds.type() == QVariant::String) {
        return addLineEdit(valueName, label);
    } else {
        qDebug("AppSettingsEditor: can't add control: unsupported control type `%s`",
               ds.typeName());
    }
    return nullptr;
}

void 
AppSettingsEditor::addCustomWidget(QWidget* wdg,
                                   const QString& label,
                                   const char* changeSignal)
{
    // add a user widget (manual reading and applying settings is required)
    customWidgets << wdg;
    lastLayout()->addRow(label, wdg);
    connect(wdg, changeSignal, this, SLOT(change()));
}

QLineEdit* 
AppSettingsEditor::addLineEdit(const QString& valueName,
                               const QString& label)
{
    if (valueName.isEmpty()) return nullptr;
    QVariant s = settings.value(valueName);

    QLineEdit* edit = new QLineEdit(s.toString());
    lastLayout()->addRow(label, edit);

    boundControls[valueName] = edit;
    connect(edit, SIGNAL(textChanged(QString)), this, SLOT(change()));

    return edit;
}

QCheckBox* 
AppSettingsEditor::addCheckBox(const QString& valueName, const QString&
                               label)
{
    if (valueName.isEmpty()) return nullptr;
    QVariant s = settings.value(valueName);

    QCheckBox* edit = new QCheckBox;
    edit->setChecked(s.toBool());
    lastLayout()->addRow(label, edit);

    boundControls[valueName] = edit;
    connect(edit, SIGNAL(toggled(bool)), this, SLOT(change()));

    return edit;
}

QSpinBox* 
AppSettingsEditor::addSpinBox(const QString& valueName,
                              const QString& label,
                              int minValue,
                              int MaxValue)
{
    if (valueName.isEmpty()) return nullptr;
    QVariant s = settings.value(valueName);

    QSpinBox* edit = new QSpinBox;
    edit->setMinimum(minValue);
    edit->setMaximum(MaxValue);
    edit->setValue(s.toInt());
    edit->setMaximumWidth(70);

    lastLayout()->addRow(label, edit);

    boundControls[valueName] = edit;
    connect(edit, SIGNAL(valueChanged(int)), this, SLOT(change()));

    return edit;
}

QDoubleSpinBox*
AppSettingsEditor::addDoubleSpinBox(const QString& valueName,
                                    const QString& label,
                                    double minValue,
                                    double maxValue,
                                    double step /*=0.1*/)
{
    if (valueName.isEmpty()) return nullptr;
    QVariant s = settings.value(valueName);

    QDoubleSpinBox* edit = new QDoubleSpinBox;
    edit->setMinimum(minValue);
    edit->setMaximum(maxValue);
    edit->setValue(s.toDouble());
    edit->setMaximumWidth(90);
    edit->setSingleStep(step);
    edit->setSuffix(" deg");

    lastLayout()->addRow(label, edit);

    boundControls[valueName] = edit;
    connect(edit, SIGNAL(valueChanged(double)), this, SLOT(change()));

    return edit;
}

QComboBox*
AppSettingsEditor::addComboBox(const QString& valueName, const QString& label, QMap<QString, QVariant> values)
{
    if (valueName.isEmpty()) return nullptr;
    QVariant s = settings.value(valueName);

    QComboBox* edit = new QComboBox;

    edit->setEditable(false);
    edit->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

    int i = 0;
    for (const QString& str : values.keys())
    {
        edit->addItem(str, values[str]);
        if (values[str] == s) edit->setCurrentIndex(i);
        i++;
    }

    lastLayout()->addRow(label, edit);

    boundControls[valueName] = edit;
    connect(edit, SIGNAL(currentIndexChanged(int)), this, SLOT(change()));

    return edit;
}

void AppSettingsEditor::addLabel(const QString& label)
{
    if (label.isEmpty()) return;
    QLabel* l = new QLabel(label);
    lastLayout()->addWidget(l);
}

void AppSettingsEditor::addSpacing(int spacing)
{
    lastLayout()->addItem(new QSpacerItem(1, spacing, QSizePolicy::Fixed, QSizePolicy::Fixed));
}

void AppSettingsEditor::applySettings()
{
    if (!changed) return;

    for (auto i = boundControls.begin(); i != boundControls.end(); ++i) {
        auto w = i.value();

        QVariant value;
        if (auto le = qobject_cast<QLineEdit*>(w)) {
            value = le->text();
        } else if (auto cb = qobject_cast<QCheckBox*>(w)) {
            value = cb->isChecked();
        } else if (auto sb = qobject_cast<QSpinBox*>(w)) {
            value = sb->value();
        } else if (auto dsb = qobject_cast<QDoubleSpinBox*>(w)) {
            value = dsb->value();
        } else if (auto cmb = qobject_cast<QComboBox*>(w)) {
            value = cmb->currentData(); // no displayRole?
        }

        settings.setValue(i.key(), value);
    }

    changed = false;
    bApply->setEnabled(false);
    emit apply(settings);
    customObj->applySettings(settings);
}

void AppSettingsEditor::updateControls()
{
    for (auto i = boundControls.begin(); i != boundControls.end(); ++i) {
        QWidget* w = i.value();

        QVariant value = settings.value(i.key());
        if (auto le = qobject_cast<QLineEdit*>(w)) {
            le->setText(value.toString());
        } else if (auto cb = qobject_cast<QCheckBox*>(w)) {
            cb->setChecked(value.toBool());
        } else if (auto sb = qobject_cast<QSpinBox*>(w)) {
            sb->setValue(value.toInt());
        } else if (auto dsb = qobject_cast<QDoubleSpinBox*>(w)) {
            dsb->setValue(value.toDouble());
        } else if (auto cmb = qobject_cast<QComboBox*>(w)) {
            cmb->setCurrentIndex(cmb->findData(value));
        }
    }
}

void AppSettingsEditor::restoreDefaults()
{
    settings = defaultSettings;
    changed = true;
    bApply->setEnabled(true);
    updateControls();
}


/* =========================== CUSTOMIZABLE CLASS =================================== */

Customizable::Customizable()
{
}

void Customizable::openSettingsEditor()
{
    AppSettingsEditor* ed = new AppSettingsEditor();
    ed->setObject(this);
    setupSettingsEditor(ed);

    ed->exec();
    ed->deleteLater();
}

void Customizable::saveSettings(const QString& iniFile)
{
    currentSettings().save(iniFile);
}

void Customizable::loadSettings(const QString& iniFile)
{
    AppSettings s = defaultSettings();
    s.load(iniFile);
    applySettings(s);
}
