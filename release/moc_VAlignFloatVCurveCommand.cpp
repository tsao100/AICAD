/****************************************************************************
** Meta object code from reading C++ file 'VAlignFloatVCurveCommand.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/command/alignment/VAlignFloatVCurveCommand.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'VAlignFloatVCurveCommand.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__command__VAlignFloatVCurveCommand_t {
    QByteArrayData data[12];
    char stringdata0[122];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__VAlignFloatVCurveCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__VAlignFloatVCurveCommand_t qt_meta_stringdata_aicad__command__VAlignFloatVCurveCommand = {
    {
QT_MOC_LITERAL(0, 0, 40), // "aicad::command::VAlignFloatVC..."
QT_MOC_LITERAL(1, 41, 10), // "onVipAdded"
QT_MOC_LITERAL(2, 52, 0), // ""
QT_MOC_LITERAL(3, 53, 2), // "ch"
QT_MOC_LITERAL(4, 56, 2), // "el"
QT_MOC_LITERAL(5, 59, 13), // "onChainageSet"
QT_MOC_LITERAL(6, 73, 11), // "onKValueSet"
QT_MOC_LITERAL(7, 85, 1), // "K"
QT_MOC_LITERAL(8, 87, 8), // "onLvcSet"
QT_MOC_LITERAL(9, 96, 3), // "lvc"
QT_MOC_LITERAL(10, 100, 16), // "onCommandEntered"
QT_MOC_LITERAL(11, 117, 4) // "text"

    },
    "aicad::command::VAlignFloatVCurveCommand\0"
    "onVipAdded\0\0ch\0el\0onChainageSet\0"
    "onKValueSet\0K\0onLvcSet\0lvc\0onCommandEntered\0"
    "text"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__VAlignFloatVCurveCommand[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    2,   39,    2, 0x08 /* Private */,
       5,    1,   44,    2, 0x08 /* Private */,
       6,    1,   47,    2, 0x08 /* Private */,
       8,    1,   50,    2, 0x08 /* Private */,
      10,    1,   53,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Double, QMetaType::Double,    3,    4,
    QMetaType::Void, QMetaType::Double,    3,
    QMetaType::Void, QMetaType::Double,    7,
    QMetaType::Void, QMetaType::Double,    9,
    QMetaType::Void, QMetaType::QString,   11,

       0        // eod
};

void aicad::command::VAlignFloatVCurveCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<VAlignFloatVCurveCommand *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onVipAdded((*reinterpret_cast< double(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2]))); break;
        case 1: _t->onChainageSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 2: _t->onKValueSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 3: _t->onLvcSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 4: _t->onCommandEntered((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::VAlignFloatVCurveCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__VAlignFloatVCurveCommand.data,
    qt_meta_data_aicad__command__VAlignFloatVCurveCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::VAlignFloatVCurveCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::VAlignFloatVCurveCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__VAlignFloatVCurveCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::VAlignFloatVCurveCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
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
QT_WARNING_POP
QT_END_MOC_NAMESPACE
