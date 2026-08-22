/****************************************************************************
** Meta object code from reading C++ file 'VAlignSetKCommand.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/command/alignment/VAlignSetKCommand.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'VAlignSetKCommand.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__command__VAlignSetKCommand_t {
    QByteArrayData data[10];
    char stringdata0[102];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__VAlignSetKCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__VAlignSetKCommand_t qt_meta_stringdata_aicad__command__VAlignSetKCommand = {
    {
QT_MOC_LITERAL(0, 0, 33), // "aicad::command::VAlignSetKCom..."
QT_MOC_LITERAL(1, 34, 13), // "onVipSelected"
QT_MOC_LITERAL(2, 48, 0), // ""
QT_MOC_LITERAL(3, 49, 3), // "idx"
QT_MOC_LITERAL(4, 53, 11), // "onKValueSet"
QT_MOC_LITERAL(5, 65, 1), // "K"
QT_MOC_LITERAL(6, 67, 8), // "onLvcSet"
QT_MOC_LITERAL(7, 76, 3), // "lvc"
QT_MOC_LITERAL(8, 80, 16), // "onCommandEntered"
QT_MOC_LITERAL(9, 97, 4) // "text"

    },
    "aicad::command::VAlignSetKCommand\0"
    "onVipSelected\0\0idx\0onKValueSet\0K\0"
    "onLvcSet\0lvc\0onCommandEntered\0text"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__VAlignSetKCommand[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    1,   34,    2, 0x08 /* Private */,
       4,    1,   37,    2, 0x08 /* Private */,
       6,    1,   40,    2, 0x08 /* Private */,
       8,    1,   43,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Double,    5,
    QMetaType::Void, QMetaType::Double,    7,
    QMetaType::Void, QMetaType::QString,    9,

       0        // eod
};

void aicad::command::VAlignSetKCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<VAlignSetKCommand *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onVipSelected((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->onKValueSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 2: _t->onLvcSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 3: _t->onCommandEntered((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::VAlignSetKCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__VAlignSetKCommand.data,
    qt_meta_data_aicad__command__VAlignSetKCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::VAlignSetKCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::VAlignSetKCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__VAlignSetKCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::VAlignSetKCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 4;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
