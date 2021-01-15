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

#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QMap>
#include <QString>
#include <QVariant>
#include <QDialog>

class QTabWidget;
class Customizable;
class QFormLayout;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;


/* =========================== APP SETTINGS ========================================= */

class AppSettings
{
    private:
        QMap<QString, QVariant> _values;

    public:
        AppSettings ();

        void setValue (const QString& name,
                       const QVariant& value);
        const QVariant value (const QString& name,
                              const QVariant& defaultValue = QVariant()) const;
        bool contains(const QString& name) const { return _values.contains(name); }

        const QMap<QString, QVariant>& values() const { return _values; }
        void setValues (const AppSettings&);

        void load (const QString& fileName);
        void save (const QString& fileName);

        AppSettings& operator<< (const AppSettings& s) { this->setValues(s); return *this; }
};


/* =========================== APP SETTINGS EDITOR ================================== */

class AppSettingsEditor : public QDialog
{
    Q_OBJECT

private:
    Customizable* customObj;
    AppSettings settings;
    AppSettings defaultSettings;  // нужен для восстановления значений, а также проверки типов
    QMap <QString, QWidget*> boundControls;
    QList<QWidget*> customWidgets;  // widgets added by addCustomWidget() method
    bool changed;

    QTabWidget* tabs;
    QPushButton* ok;
    QPushButton* cancel;
    QPushButton* bApply;
    QPushButton* setDefault;

    void updateControls();
    QFormLayout* lastLayout();

    private slots:
    void change();         // помечает текущие настройки как несохранённые
    void applySettings();
    void restoreDefaults();

signals:
    void apply(AppSettings&);

public:
    AppSettingsEditor();
    ~AppSettingsEditor();

    void addTab(const QString& tabName);
    //void beginGroup  ( const QString& groupName );
    void endGroup();
    void addSpacing(int spacing);
    QWidget* addControl(const QString& valueName, const QString& label);
    void addCustomWidget(QWidget* wdg, const QString& label, const char* changeSignal);
    QLineEdit* addLineEdit(const QString& valueName, const QString& label);
    QCheckBox* addCheckBox(const QString& valueName, const QString& label);
    QSpinBox* addSpinBox(const QString& valueName, const QString& label, int minValue, int MaxValue);
    QDoubleSpinBox* addDoubleSpinBox(const QString& valueName, const QString& label, double minValue, double maxValue, double step = 0.1);
    QComboBox* addComboBox(const QString& valueName, const QString& label, QMap<QString, QVariant> values);
    void addLabel(const QString& label);

    void setObject(Customizable* obj);

};


/* =========================== CUSTOMIZABLE CLASS =================================== */

class Customizable
{
    public:
        Customizable ();
        virtual ~Customizable() { }

        virtual AppSettings defaultSettings() { return AppSettings(); }
        virtual AppSettings currentSettings() { return AppSettings(); }
        virtual void applySettings ( const AppSettings& )         { }
        virtual void setupSettingsEditor  ( AppSettingsEditor* )  { }

        void loadSettings (const QString& iniFile = "settings.ini");
        void saveSettings (const QString& iniFile = "settings.ini");
        void openSettingsEditor();
};

#endif // APPSETTINGS_H
