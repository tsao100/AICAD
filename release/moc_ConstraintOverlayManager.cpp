/****************************************************************************
** Meta object code from reading C++ file 'ConstraintOverlayManager.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/sketch/ConstraintOverlayManager.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ConstraintOverlayManager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__ConstraintOverlayManager_t {
    QByteArrayData data[11];
    char stringdata0[186];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__ConstraintOverlayManager_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__ConstraintOverlayManager_t qt_meta_stringdata_aicad__cad__ConstraintOverlayManager = {
    {
QT_MOC_LITERAL(0, 0, 36), // "aicad::cad::ConstraintOverlay..."
QT_MOC_LITERAL(1, 37, 26), // "dimensionConstraintClicked"
QT_MOC_LITERAL(2, 64, 0), // ""
QT_MOC_LITERAL(3, 65, 14), // "constraintUuid"
QT_MOC_LITERAL(4, 80, 30), // "ConstraintOverlayManager::Mode"
QT_MOC_LITERAL(5, 111, 4), // "mode"
QT_MOC_LITERAL(6, 116, 10), // "instanceId"
QT_MOC_LITERAL(7, 127, 15), // "onSourceRebuilt"
QT_MOC_LITERAL(8, 143, 17), // "onConstraintAdded"
QT_MOC_LITERAL(9, 161, 4), // "uuid"
QT_MOC_LITERAL(10, 166, 19) // "onConstraintRemoved"

    },
    "aicad::cad::ConstraintOverlayManager\0"
    "dimensionConstraintClicked\0\0constraintUuid\0"
    "ConstraintOverlayManager::Mode\0mode\0"
    "instanceId\0onSourceRebuilt\0onConstraintAdded\0"
    "uuid\0onConstraintRemoved"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__ConstraintOverlayManager[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    3,   34,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       7,    0,   41,    2, 0x08 /* Private */,
       8,    1,   42,    2, 0x08 /* Private */,
      10,    1,   45,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString, 0x80000000 | 4, QMetaType::QString,    3,    5,    6,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    9,
    QMetaType::Void, QMetaType::QString,    9,

       0        // eod
};

void aicad::cad::ConstraintOverlayManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ConstraintOverlayManager *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->dimensionConstraintClicked((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< ConstraintOverlayManager::Mode(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 1: _t->onSourceRebuilt(); break;
        case 2: _t->onConstraintAdded((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->onConstraintRemoved((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ConstraintOverlayManager::*)(const QString & , ConstraintOverlayManager::Mode , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ConstraintOverlayManager::dimensionConstraintClicked)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::ConstraintOverlayManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__ConstraintOverlayManager.data,
    qt_meta_data_aicad__cad__ConstraintOverlayManager,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::ConstraintOverlayManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::ConstraintOverlayManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__ConstraintOverlayManager.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::cad::ConstraintOverlayManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
void aicad::cad::ConstraintOverlayManager::dimensionConstraintClicked(const QString & _t1, ConstraintOverlayManager::Mode _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
