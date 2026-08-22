/****************************************************************************
** Meta object code from reading C++ file 'Sketch.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/Sketch.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Sketch.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__Sketch_t {
    QByteArrayData data[18];
    char stringdata0[242];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__Sketch_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__Sketch_t qt_meta_stringdata_aicad__cad__Sketch = {
    {
QT_MOC_LITERAL(0, 0, 18), // "aicad::cad::Sketch"
QT_MOC_LITERAL(1, 19, 12), // "planeChanged"
QT_MOC_LITERAL(2, 32, 0), // ""
QT_MOC_LITERAL(3, 33, 6), // "Plane*"
QT_MOC_LITERAL(4, 40, 5), // "plane"
QT_MOC_LITERAL(5, 46, 15), // "geometryChanged"
QT_MOC_LITERAL(6, 62, 7), // "rebuilt"
QT_MOC_LITERAL(7, 70, 15), // "constraintAdded"
QT_MOC_LITERAL(8, 86, 4), // "uuid"
QT_MOC_LITERAL(9, 91, 17), // "constraintRemoved"
QT_MOC_LITERAL(10, 109, 15), // "annotationAdded"
QT_MOC_LITERAL(11, 125, 17), // "annotationRemoved"
QT_MOC_LITERAL(12, 143, 16), // "constraintSolved"
QT_MOC_LITERAL(13, 160, 11), // "SolveResult"
QT_MOC_LITERAL(14, 172, 6), // "result"
QT_MOC_LITERAL(15, 179, 15), // "scheduleRebuild"
QT_MOC_LITERAL(16, 195, 23), // "onPlaneAboutToBeDeleted"
QT_MOC_LITERAL(17, 219, 22) // "onPlaneGeometryChanged"

    },
    "aicad::cad::Sketch\0planeChanged\0\0"
    "Plane*\0plane\0geometryChanged\0rebuilt\0"
    "constraintAdded\0uuid\0constraintRemoved\0"
    "annotationAdded\0annotationRemoved\0"
    "constraintSolved\0SolveResult\0result\0"
    "scheduleRebuild\0onPlaneAboutToBeDeleted\0"
    "onPlaneGeometryChanged"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__Sketch[] = {

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
       5,    0,   72,    2, 0x06 /* Public */,
       6,    0,   73,    2, 0x06 /* Public */,
       7,    1,   74,    2, 0x06 /* Public */,
       9,    1,   77,    2, 0x06 /* Public */,
      10,    1,   80,    2, 0x06 /* Public */,
      11,    1,   83,    2, 0x06 /* Public */,
      12,    1,   86,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      15,    0,   89,    2, 0x0a /* Public */,
      16,    0,   90,    2, 0x08 /* Private */,
      17,    0,   91,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, 0x80000000 | 13,   14,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void aicad::cad::Sketch::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Sketch *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->planeChanged((*reinterpret_cast< Plane*(*)>(_a[1]))); break;
        case 1: _t->geometryChanged(); break;
        case 2: _t->rebuilt(); break;
        case 3: _t->constraintAdded((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->constraintRemoved((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 5: _t->annotationAdded((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 6: _t->annotationRemoved((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 7: _t->constraintSolved((*reinterpret_cast< SolveResult(*)>(_a[1]))); break;
        case 8: _t->scheduleRebuild(); break;
        case 9: _t->onPlaneAboutToBeDeleted(); break;
        case 10: _t->onPlaneGeometryChanged(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< Plane* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Sketch::*)(Plane * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Sketch::planeChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (Sketch::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Sketch::geometryChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (Sketch::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Sketch::rebuilt)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (Sketch::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Sketch::constraintAdded)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (Sketch::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Sketch::constraintRemoved)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (Sketch::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Sketch::annotationAdded)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (Sketch::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Sketch::annotationRemoved)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (Sketch::*)(SolveResult );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Sketch::constraintSolved)) {
                *result = 7;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::Sketch::staticMetaObject = { {
    QMetaObject::SuperData::link<Feature::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__Sketch.data,
    qt_meta_data_aicad__cad__Sketch,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::Sketch::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::Sketch::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__Sketch.stringdata0))
        return static_cast<void*>(this);
    return Feature::qt_metacast(_clname);
}

int aicad::cad::Sketch::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Feature::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    }
    return _id;
}

// SIGNAL 0
void aicad::cad::Sketch::planeChanged(Plane * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::cad::Sketch::geometryChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void aicad::cad::Sketch::rebuilt()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void aicad::cad::Sketch::constraintAdded(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::cad::Sketch::constraintRemoved(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void aicad::cad::Sketch::annotationAdded(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void aicad::cad::Sketch::annotationRemoved(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void aicad::cad::Sketch::constraintSolved(SolveResult _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
