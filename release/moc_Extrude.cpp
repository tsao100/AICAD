/****************************************************************************
** Meta object code from reading C++ file 'Extrude.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/Extrude.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Extrude.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__Extrude_t {
    QByteArrayData data[9];
    char stringdata0[96];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__Extrude_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__Extrude_t qt_meta_stringdata_aicad__cad__Extrude = {
    {
QT_MOC_LITERAL(0, 0, 19), // "aicad::cad::Extrude"
QT_MOC_LITERAL(1, 20, 13), // "heightChanged"
QT_MOC_LITERAL(2, 34, 0), // ""
QT_MOC_LITERAL(3, 35, 6), // "height"
QT_MOC_LITERAL(4, 42, 15), // "reversedChanged"
QT_MOC_LITERAL(5, 58, 8), // "reversed"
QT_MOC_LITERAL(6, 67, 13), // "sketchChanged"
QT_MOC_LITERAL(7, 81, 7), // "Sketch*"
QT_MOC_LITERAL(8, 89, 6) // "sketch"

    },
    "aicad::cad::Extrude\0heightChanged\0\0"
    "height\0reversedChanged\0reversed\0"
    "sketchChanged\0Sketch*\0sketch"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__Extrude[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       2,   38, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   29,    2, 0x06 /* Public */,
       4,    1,   32,    2, 0x06 /* Public */,
       6,    1,   35,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Double,    3,
    QMetaType::Void, QMetaType::Bool,    5,
    QMetaType::Void, 0x80000000 | 7,    8,

 // properties: name, type, flags
       3, QMetaType::Double, 0x00495103,
       5, QMetaType::Bool, 0x00495103,

 // properties: notify_signal_id
       0,
       1,

       0        // eod
};

void aicad::cad::Extrude::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Extrude *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->heightChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 1: _t->reversedChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->sketchChanged((*reinterpret_cast< Sketch*(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Extrude::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Extrude::heightChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (Extrude::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Extrude::reversedChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (Extrude::*)(Sketch * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Extrude::sketchChanged)) {
                *result = 2;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Extrude *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< double*>(_v) = _t->height(); break;
        case 1: *reinterpret_cast< bool*>(_v) = _t->isReversed(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Extrude *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setHeight(*reinterpret_cast< double*>(_v)); break;
        case 1: _t->setReversed(*reinterpret_cast< bool*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::Extrude::staticMetaObject = { {
    QMetaObject::SuperData::link<Feature::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__Extrude.data,
    qt_meta_data_aicad__cad__Extrude,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::Extrude::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::Extrude::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__Extrude.stringdata0))
        return static_cast<void*>(this);
    return Feature::qt_metacast(_clname);
}

int aicad::cad::Extrude::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Feature::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 3;
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 2;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void aicad::cad::Extrude::heightChanged(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::cad::Extrude::reversedChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::cad::Extrude::sketchChanged(Sketch * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
