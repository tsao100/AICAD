/****************************************************************************
** Meta object code from reading C++ file 'SketchInstance.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/SketchInstance.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'SketchInstance.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__SketchInstance_t {
    QByteArrayData data[7];
    char stringdata0[109];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__SketchInstance_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__SketchInstance_t qt_meta_stringdata_aicad__cad__SketchInstance = {
    {
QT_MOC_LITERAL(0, 0, 26), // "aicad::cad::SketchInstance"
QT_MOC_LITERAL(1, 27, 15), // "instanceRebuilt"
QT_MOC_LITERAL(2, 43, 0), // ""
QT_MOC_LITERAL(3, 44, 15), // "overrideChanged"
QT_MOC_LITERAL(4, 60, 9), // "paramName"
QT_MOC_LITERAL(5, 70, 15), // "onMasterRebuilt"
QT_MOC_LITERAL(6, 86, 22) // "onMasterAboutToDestroy"

    },
    "aicad::cad::SketchInstance\0instanceRebuilt\0"
    "\0overrideChanged\0paramName\0onMasterRebuilt\0"
    "onMasterAboutToDestroy"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__SketchInstance[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   34,    2, 0x06 /* Public */,
       3,    1,   35,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       5,    0,   38,    2, 0x08 /* Private */,
       6,    0,   39,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    4,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void aicad::cad::SketchInstance::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SketchInstance *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->instanceRebuilt(); break;
        case 1: _t->overrideChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 2: _t->onMasterRebuilt(); break;
        case 3: _t->onMasterAboutToDestroy(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (SketchInstance::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchInstance::instanceRebuilt)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (SketchInstance::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchInstance::overrideChanged)) {
                *result = 1;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::SketchInstance::staticMetaObject = { {
    QMetaObject::SuperData::link<Feature::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__SketchInstance.data,
    qt_meta_data_aicad__cad__SketchInstance,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::SketchInstance::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::SketchInstance::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__SketchInstance.stringdata0))
        return static_cast<void*>(this);
    return Feature::qt_metacast(_clname);
}

int aicad::cad::SketchInstance::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Feature::qt_metacall(_c, _id, _a);
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

// SIGNAL 0
void aicad::cad::SketchInstance::instanceRebuilt()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void aicad::cad::SketchInstance::overrideChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
