/****************************************************************************
** Meta object code from reading C++ file 'CommandLineWidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/ui/CommandLineWidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CommandLineWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__ui__CommandLineWidget_t {
    QByteArrayData data[11];
    char stringdata0[188];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__ui__CommandLineWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__ui__CommandLineWidget_t qt_meta_stringdata_aicad__ui__CommandLineWidget = {
    {
QT_MOC_LITERAL(0, 0, 28), // "aicad::ui::CommandLineWidget"
QT_MOC_LITERAL(1, 29, 16), // "commandSubmitted"
QT_MOC_LITERAL(2, 46, 0), // ""
QT_MOC_LITERAL(3, 47, 3), // "cmd"
QT_MOC_LITERAL(4, 51, 14), // "optionSelected"
QT_MOC_LITERAL(5, 66, 3), // "key"
QT_MOC_LITERAL(6, 70, 21), // "historyPopupRequested"
QT_MOC_LITERAL(7, 92, 22), // "onHistoryButtonClicked"
QT_MOC_LITERAL(8, 115, 22), // "onPromptOptionsChanged"
QT_MOC_LITERAL(9, 138, 41), // "QList<command::InputParser::P..."
QT_MOC_LITERAL(10, 180, 7) // "options"

    },
    "aicad::ui::CommandLineWidget\0"
    "commandSubmitted\0\0cmd\0optionSelected\0"
    "key\0historyPopupRequested\0"
    "onHistoryButtonClicked\0onPromptOptionsChanged\0"
    "QList<command::InputParser::ParsedOption>\0"
    "options"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__ui__CommandLineWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   39,    2, 0x06 /* Public */,
       4,    1,   42,    2, 0x06 /* Public */,
       6,    0,   45,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       7,    0,   46,    2, 0x0a /* Public */,
       8,    1,   47,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 9,   10,

       0        // eod
};

void aicad::ui::CommandLineWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<CommandLineWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->commandSubmitted((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->optionSelected((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 2: _t->historyPopupRequested(); break;
        case 3: _t->onHistoryButtonClicked(); break;
        case 4: _t->onPromptOptionsChanged((*reinterpret_cast< const QList<command::InputParser::ParsedOption>(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (CommandLineWidget::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineWidget::commandSubmitted)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (CommandLineWidget::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineWidget::optionSelected)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (CommandLineWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineWidget::historyPopupRequested)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::ui::CommandLineWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_aicad__ui__CommandLineWidget.data,
    qt_meta_data_aicad__ui__CommandLineWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::ui::CommandLineWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::ui::CommandLineWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__ui__CommandLineWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int aicad::ui::CommandLineWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void aicad::ui::CommandLineWidget::commandSubmitted(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::ui::CommandLineWidget::optionSelected(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::ui::CommandLineWidget::historyPopupRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
