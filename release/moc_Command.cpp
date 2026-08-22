/****************************************************************************
** Meta object code from reading C++ file 'Command.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/command/Command.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Command.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__command__Command_t {
    QByteArrayData data[17];
    char stringdata0[182];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__Command_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__Command_t qt_meta_stringdata_aicad__command__Command = {
    {
QT_MOC_LITERAL(0, 0, 23), // "aicad::command::Command"
QT_MOC_LITERAL(1, 24, 7), // "started"
QT_MOC_LITERAL(2, 32, 0), // ""
QT_MOC_LITERAL(3, 33, 8), // "finished"
QT_MOC_LITERAL(4, 42, 13), // "CommandResult"
QT_MOC_LITERAL(5, 56, 6), // "result"
QT_MOC_LITERAL(6, 63, 9), // "cancelled"
QT_MOC_LITERAL(7, 73, 15), // "progressChanged"
QT_MOC_LITERAL(8, 89, 8), // "progress"
QT_MOC_LITERAL(9, 98, 7), // "message"
QT_MOC_LITERAL(10, 106, 12), // "stateChanged"
QT_MOC_LITERAL(11, 119, 12), // "CommandState"
QT_MOC_LITERAL(12, 132, 5), // "state"
QT_MOC_LITERAL(13, 138, 13), // "messageOutput"
QT_MOC_LITERAL(14, 152, 15), // "progressUpdated"
QT_MOC_LITERAL(15, 168, 7), // "current"
QT_MOC_LITERAL(16, 176, 5) // "total"

    },
    "aicad::command::Command\0started\0\0"
    "finished\0CommandResult\0result\0cancelled\0"
    "progressChanged\0progress\0message\0"
    "stateChanged\0CommandState\0state\0"
    "messageOutput\0progressUpdated\0current\0"
    "total"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__Command[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   49,    2, 0x06 /* Public */,
       3,    1,   50,    2, 0x06 /* Public */,
       6,    0,   53,    2, 0x06 /* Public */,
       7,    2,   54,    2, 0x06 /* Public */,
      10,    1,   59,    2, 0x06 /* Public */,
      13,    1,   62,    2, 0x06 /* Public */,
      14,    3,   65,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 4,    5,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::QString,    8,    9,
    QMetaType::Void, 0x80000000 | 11,   12,
    QMetaType::Void, QMetaType::QString,    9,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::QString,   15,   16,    9,

       0        // eod
};

void aicad::command::Command::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Command *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->started(); break;
        case 1: _t->finished((*reinterpret_cast< const CommandResult(*)>(_a[1]))); break;
        case 2: _t->cancelled(); break;
        case 3: _t->progressChanged((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 4: _t->stateChanged((*reinterpret_cast< CommandState(*)>(_a[1]))); break;
        case 5: _t->messageOutput((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 6: _t->progressUpdated((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Command::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Command::started)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (Command::*)(const CommandResult & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Command::finished)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (Command::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Command::cancelled)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (Command::*)(int , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Command::progressChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (Command::*)(CommandState );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Command::stateChanged)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (Command::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Command::messageOutput)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (Command::*)(int , int , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Command::progressUpdated)) {
                *result = 6;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::Command::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__Command.data,
    qt_meta_data_aicad__command__Command,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::Command::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::Command::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__Command.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::command::Command::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void aicad::command::Command::started()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void aicad::command::Command::finished(const CommandResult & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::command::Command::cancelled()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void aicad::command::Command::progressChanged(int _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::command::Command::stateChanged(CommandState _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void aicad::command::Command::messageOutput(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void aicad::command::Command::progressUpdated(int _t1, int _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
