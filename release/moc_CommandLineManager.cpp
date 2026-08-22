/****************************************************************************
** Meta object code from reading C++ file 'CommandLineManager.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/core/CommandLineManager.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CommandLineManager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__core__CommandLineManager_t {
    QByteArrayData data[24];
    char stringdata0[324];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__core__CommandLineManager_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__core__CommandLineManager_t qt_meta_stringdata_aicad__core__CommandLineManager = {
    {
QT_MOC_LITERAL(0, 0, 31), // "aicad::core::CommandLineManager"
QT_MOC_LITERAL(1, 32, 13), // "promptChanged"
QT_MOC_LITERAL(2, 46, 0), // ""
QT_MOC_LITERAL(3, 47, 6), // "prompt"
QT_MOC_LITERAL(4, 54, 16), // "optionsAvailable"
QT_MOC_LITERAL(5, 71, 7), // "options"
QT_MOC_LITERAL(6, 79, 15), // "messageReceived"
QT_MOC_LITERAL(7, 95, 3), // "msg"
QT_MOC_LITERAL(8, 99, 11), // "MessageType"
QT_MOC_LITERAL(9, 111, 4), // "type"
QT_MOC_LITERAL(10, 116, 13), // "inputRequired"
QT_MOC_LITERAL(11, 130, 9), // "InputType"
QT_MOC_LITERAL(12, 140, 14), // "commandStarted"
QT_MOC_LITERAL(13, 155, 3), // "cmd"
QT_MOC_LITERAL(14, 159, 15), // "commandFinished"
QT_MOC_LITERAL(15, 175, 7), // "success"
QT_MOC_LITERAL(16, 183, 16), // "commandCancelled"
QT_MOC_LITERAL(17, 200, 20), // "promptOptionsChanged"
QT_MOC_LITERAL(18, 221, 41), // "QList<command::InputParser::P..."
QT_MOC_LITERAL(19, 263, 14), // "onCommandInput"
QT_MOC_LITERAL(20, 278, 5), // "input"
QT_MOC_LITERAL(21, 284, 16), // "onOptionSelected"
QT_MOC_LITERAL(22, 301, 6), // "option"
QT_MOC_LITERAL(23, 308, 15) // "onEscapePressed"

    },
    "aicad::core::CommandLineManager\0"
    "promptChanged\0\0prompt\0optionsAvailable\0"
    "options\0messageReceived\0msg\0MessageType\0"
    "type\0inputRequired\0InputType\0"
    "commandStarted\0cmd\0commandFinished\0"
    "success\0commandCancelled\0promptOptionsChanged\0"
    "QList<command::InputParser::ParsedOption>\0"
    "onCommandInput\0input\0onOptionSelected\0"
    "option\0onEscapePressed"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__core__CommandLineManager[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      11,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       8,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   69,    2, 0x06 /* Public */,
       4,    1,   72,    2, 0x06 /* Public */,
       6,    2,   75,    2, 0x06 /* Public */,
      10,    1,   80,    2, 0x06 /* Public */,
      12,    1,   83,    2, 0x06 /* Public */,
      14,    2,   86,    2, 0x06 /* Public */,
      16,    0,   91,    2, 0x06 /* Public */,
      17,    1,   92,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      19,    1,   95,    2, 0x0a /* Public */,
      21,    1,   98,    2, 0x0a /* Public */,
      23,    0,  101,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QStringList,    5,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 8,    7,    9,
    QMetaType::Void, 0x80000000 | 11,    9,
    QMetaType::Void, QMetaType::QString,   13,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,   13,   15,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 18,    5,

 // slots: parameters
    QMetaType::Void, QMetaType::QString,   20,
    QMetaType::Void, QMetaType::QString,   22,
    QMetaType::Void,

       0        // eod
};

void aicad::core::CommandLineManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<CommandLineManager *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->promptChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->optionsAvailable((*reinterpret_cast< const QStringList(*)>(_a[1]))); break;
        case 2: _t->messageReceived((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< MessageType(*)>(_a[2]))); break;
        case 3: _t->inputRequired((*reinterpret_cast< InputType(*)>(_a[1]))); break;
        case 4: _t->commandStarted((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 5: _t->commandFinished((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 6: _t->commandCancelled(); break;
        case 7: _t->promptOptionsChanged((*reinterpret_cast< const QList<command::InputParser::ParsedOption>(*)>(_a[1]))); break;
        case 8: _t->onCommandInput((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 9: _t->onOptionSelected((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 10: _t->onEscapePressed(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (CommandLineManager::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineManager::promptChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (CommandLineManager::*)(const QStringList & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineManager::optionsAvailable)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (CommandLineManager::*)(const QString & , MessageType );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineManager::messageReceived)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (CommandLineManager::*)(InputType );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineManager::inputRequired)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (CommandLineManager::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineManager::commandStarted)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (CommandLineManager::*)(const QString & , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineManager::commandFinished)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (CommandLineManager::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineManager::commandCancelled)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (CommandLineManager::*)(const QList<command::InputParser::ParsedOption> & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandLineManager::promptOptionsChanged)) {
                *result = 7;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::core::CommandLineManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__core__CommandLineManager.data,
    qt_meta_data_aicad__core__CommandLineManager,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::core::CommandLineManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::core::CommandLineManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__core__CommandLineManager.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::core::CommandLineManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 11;
    }
    return _id;
}

// SIGNAL 0
void aicad::core::CommandLineManager::promptChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::core::CommandLineManager::optionsAvailable(const QStringList & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::core::CommandLineManager::messageReceived(const QString & _t1, MessageType _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::core::CommandLineManager::inputRequired(InputType _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::core::CommandLineManager::commandStarted(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void aicad::core::CommandLineManager::commandFinished(const QString & _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void aicad::core::CommandLineManager::commandCancelled()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void aicad::core::CommandLineManager::promptOptionsChanged(const QList<command::InputParser::ParsedOption> & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
