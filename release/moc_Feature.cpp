/****************************************************************************
** Meta object code from reading C++ file 'Feature.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/Feature.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Feature.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__Feature_t {
    QByteArrayData data[14];
    char stringdata0[171];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__Feature_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__Feature_t qt_meta_stringdata_aicad__cad__Feature = {
    {
QT_MOC_LITERAL(0, 0, 19), // "aicad::cad::Feature"
QT_MOC_LITERAL(1, 20, 11), // "nameChanged"
QT_MOC_LITERAL(2, 32, 0), // ""
QT_MOC_LITERAL(3, 33, 4), // "name"
QT_MOC_LITERAL(4, 38, 12), // "shapeChanged"
QT_MOC_LITERAL(5, 51, 17), // "visibilityChanged"
QT_MOC_LITERAL(6, 69, 7), // "visible"
QT_MOC_LITERAL(7, 77, 17), // "suppressedChanged"
QT_MOC_LITERAL(8, 95, 10), // "suppressed"
QT_MOC_LITERAL(9, 106, 12), // "errorChanged"
QT_MOC_LITERAL(10, 119, 8), // "hasError"
QT_MOC_LITERAL(11, 128, 7), // "message"
QT_MOC_LITERAL(12, 136, 16), // "rebuildRequested"
QT_MOC_LITERAL(13, 153, 17) // "dirtyStateChanged"

    },
    "aicad::cad::Feature\0nameChanged\0\0name\0"
    "shapeChanged\0visibilityChanged\0visible\0"
    "suppressedChanged\0suppressed\0errorChanged\0"
    "hasError\0message\0rebuildRequested\0"
    "dirtyStateChanged"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__Feature[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       3,   66, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   49,    2, 0x06 /* Public */,
       4,    0,   52,    2, 0x06 /* Public */,
       5,    1,   53,    2, 0x06 /* Public */,
       7,    1,   56,    2, 0x06 /* Public */,
       9,    2,   59,    2, 0x06 /* Public */,
      12,    0,   64,    2, 0x06 /* Public */,
      13,    0,   65,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    6,
    QMetaType::Void, QMetaType::Bool,    8,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   10,   11,
    QMetaType::Void,
    QMetaType::Void,

 // properties: name, type, flags
       3, QMetaType::QString, 0x00495103,
       6, QMetaType::Bool, 0x00495103,
       8, QMetaType::Bool, 0x00495103,

 // properties: notify_signal_id
       0,
       2,
       3,

       0        // eod
};

void aicad::cad::Feature::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Feature *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->nameChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->shapeChanged(); break;
        case 2: _t->visibilityChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 3: _t->suppressedChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 4: _t->errorChanged((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 5: _t->rebuildRequested(); break;
        case 6: _t->dirtyStateChanged(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Feature::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Feature::nameChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (Feature::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Feature::shapeChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (Feature::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Feature::visibilityChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (Feature::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Feature::suppressedChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (Feature::*)(bool , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Feature::errorChanged)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (Feature::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Feature::rebuildRequested)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (Feature::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Feature::dirtyStateChanged)) {
                *result = 6;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Feature *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< QString*>(_v) = _t->name(); break;
        case 1: *reinterpret_cast< bool*>(_v) = _t->isVisible(); break;
        case 2: *reinterpret_cast< bool*>(_v) = _t->isSuppressed(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Feature *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setName(*reinterpret_cast< QString*>(_v)); break;
        case 1: _t->setVisible(*reinterpret_cast< bool*>(_v)); break;
        case 2: _t->setSuppressed(*reinterpret_cast< bool*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::Feature::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__Feature.data,
    qt_meta_data_aicad__cad__Feature,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::Feature::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::Feature::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__Feature.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::cad::Feature::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 3;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void aicad::cad::Feature::nameChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::cad::Feature::shapeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void aicad::cad::Feature::visibilityChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::cad::Feature::suppressedChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::cad::Feature::errorChanged(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void aicad::cad::Feature::rebuildRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void aicad::cad::Feature::dirtyStateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
