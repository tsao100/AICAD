/****************************************************************************
** Meta object code from reading C++ file 'Plane.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/Plane.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Plane.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__Plane_t {
    QByteArrayData data[21];
    char stringdata0[191];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__Plane_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__Plane_t qt_meta_stringdata_aicad__cad__Plane = {
    {
QT_MOC_LITERAL(0, 0, 17), // "aicad::cad::Plane"
QT_MOC_LITERAL(1, 18, 11), // "nameChanged"
QT_MOC_LITERAL(2, 30, 0), // ""
QT_MOC_LITERAL(3, 31, 7), // "newName"
QT_MOC_LITERAL(4, 39, 15), // "geometryChanged"
QT_MOC_LITERAL(5, 55, 18), // "activeStateChanged"
QT_MOC_LITERAL(6, 74, 8), // "isActive"
QT_MOC_LITERAL(7, 83, 18), // "lockedStateChanged"
QT_MOC_LITERAL(8, 102, 8), // "isLocked"
QT_MOC_LITERAL(9, 111, 16), // "aboutToBeDeleted"
QT_MOC_LITERAL(10, 128, 12), // "markModified"
QT_MOC_LITERAL(11, 141, 2), // "id"
QT_MOC_LITERAL(12, 144, 4), // "name"
QT_MOC_LITERAL(13, 149, 4), // "Type"
QT_MOC_LITERAL(14, 154, 2), // "XY"
QT_MOC_LITERAL(15, 157, 2), // "YZ"
QT_MOC_LITERAL(16, 160, 2), // "ZX"
QT_MOC_LITERAL(17, 163, 2), // "XZ"
QT_MOC_LITERAL(18, 166, 6), // "Custom"
QT_MOC_LITERAL(19, 173, 6), // "Offset"
QT_MOC_LITERAL(20, 180, 10) // "ThreePoint"

    },
    "aicad::cad::Plane\0nameChanged\0\0newName\0"
    "geometryChanged\0activeStateChanged\0"
    "isActive\0lockedStateChanged\0isLocked\0"
    "aboutToBeDeleted\0markModified\0id\0name\0"
    "Type\0XY\0YZ\0ZX\0XZ\0Custom\0Offset\0"
    "ThreePoint"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__Plane[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       4,   56, // properties
       1,   72, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   44,    2, 0x06 /* Public */,
       4,    0,   47,    2, 0x06 /* Public */,
       5,    1,   48,    2, 0x06 /* Public */,
       7,    1,   51,    2, 0x06 /* Public */,
       9,    0,   54,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      10,    0,   55,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    6,
    QMetaType::Void, QMetaType::Bool,    8,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,

 // properties: name, type, flags
      11, QMetaType::QString, 0x00095401,
      12, QMetaType::QString, 0x00495103,
       6, QMetaType::Bool, 0x00495001,
       8, QMetaType::Bool, 0x00495003,

 // properties: notify_signal_id
       0,
       0,
       2,
       3,

 // enums: name, alias, flags, count, data
      13,   13, 0x2,    7,   77,

 // enum data: key, value
      14, uint(aicad::cad::Plane::Type::XY),
      15, uint(aicad::cad::Plane::Type::YZ),
      16, uint(aicad::cad::Plane::Type::ZX),
      17, uint(aicad::cad::Plane::Type::XZ),
      18, uint(aicad::cad::Plane::Type::Custom),
      19, uint(aicad::cad::Plane::Type::Offset),
      20, uint(aicad::cad::Plane::Type::ThreePoint),

       0        // eod
};

void aicad::cad::Plane::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Plane *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->nameChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->geometryChanged(); break;
        case 2: _t->activeStateChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 3: _t->lockedStateChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 4: _t->aboutToBeDeleted(); break;
        case 5: _t->markModified(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Plane::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Plane::nameChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (Plane::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Plane::geometryChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (Plane::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Plane::activeStateChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (Plane::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Plane::lockedStateChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (Plane::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Plane::aboutToBeDeleted)) {
                *result = 4;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Plane *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< QString*>(_v) = _t->id(); break;
        case 1: *reinterpret_cast< QString*>(_v) = _t->name(); break;
        case 2: *reinterpret_cast< bool*>(_v) = _t->isActive(); break;
        case 3: *reinterpret_cast< bool*>(_v) = _t->isLocked(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Plane *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 1: _t->setName(*reinterpret_cast< QString*>(_v)); break;
        case 3: _t->setLocked(*reinterpret_cast< bool*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::Plane::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__Plane.data,
    qt_meta_data_aicad__cad__Plane,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::Plane::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::Plane::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__Plane.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::cad::Plane::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 6;
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 4;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 4;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void aicad::cad::Plane::nameChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::cad::Plane::geometryChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void aicad::cad::Plane::activeStateChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::cad::Plane::lockedStateChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::cad::Plane::aboutToBeDeleted()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
