/****************************************************************************
** Meta object code from reading C++ file 'OSnapManager.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/osnap/OSnapManager.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'OSnapManager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__osnap__OSnapManager_t {
    QByteArrayData data[16];
    char stringdata0[189];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__osnap__OSnapManager_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__osnap__OSnapManager_t qt_meta_stringdata_aicad__osnap__OSnapManager = {
    {
QT_MOC_LITERAL(0, 0, 26), // "aicad::osnap::OSnapManager"
QT_MOC_LITERAL(1, 27, 10), // "snapLocked"
QT_MOC_LITERAL(2, 38, 0), // ""
QT_MOC_LITERAL(3, 39, 13), // "SnapCandidate"
QT_MOC_LITERAL(4, 53, 9), // "candidate"
QT_MOC_LITERAL(5, 63, 11), // "snapCleared"
QT_MOC_LITERAL(6, 75, 15), // "settingsChanged"
QT_MOC_LITERAL(7, 91, 13), // "OSnapSettings"
QT_MOC_LITERAL(8, 105, 8), // "settings"
QT_MOC_LITERAL(9, 114, 13), // "snapConfirmed"
QT_MOC_LITERAL(10, 128, 6), // "gp_Pnt"
QT_MOC_LITERAL(11, 135, 10), // "worldPoint"
QT_MOC_LITERAL(12, 146, 8), // "SnapType"
QT_MOC_LITERAL(13, 155, 4), // "type"
QT_MOC_LITERAL(14, 160, 13), // "onGripHovered"
QT_MOC_LITERAL(15, 174, 14) // "onGripReleased"

    },
    "aicad::osnap::OSnapManager\0snapLocked\0"
    "\0SnapCandidate\0candidate\0snapCleared\0"
    "settingsChanged\0OSnapSettings\0settings\0"
    "snapConfirmed\0gp_Pnt\0worldPoint\0"
    "SnapType\0type\0onGripHovered\0onGripReleased"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__osnap__OSnapManager[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   44,    2, 0x06 /* Public */,
       5,    0,   47,    2, 0x06 /* Public */,
       6,    1,   48,    2, 0x06 /* Public */,
       9,    2,   51,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      14,    0,   56,    2, 0x08 /* Private */,
      15,    0,   57,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void, 0x80000000 | 10, 0x80000000 | 12,   11,   13,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void aicad::osnap::OSnapManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<OSnapManager *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->snapLocked((*reinterpret_cast< const SnapCandidate(*)>(_a[1]))); break;
        case 1: _t->snapCleared(); break;
        case 2: _t->settingsChanged((*reinterpret_cast< const OSnapSettings(*)>(_a[1]))); break;
        case 3: _t->snapConfirmed((*reinterpret_cast< const gp_Pnt(*)>(_a[1])),(*reinterpret_cast< SnapType(*)>(_a[2]))); break;
        case 4: _t->onGripHovered(); break;
        case 5: _t->onGripReleased(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (OSnapManager::*)(const SnapCandidate & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&OSnapManager::snapLocked)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (OSnapManager::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&OSnapManager::snapCleared)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (OSnapManager::*)(const OSnapSettings & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&OSnapManager::settingsChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (OSnapManager::*)(const gp_Pnt & , SnapType );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&OSnapManager::snapConfirmed)) {
                *result = 3;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::osnap::OSnapManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__osnap__OSnapManager.data,
    qt_meta_data_aicad__osnap__OSnapManager,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::osnap::OSnapManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::osnap::OSnapManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__osnap__OSnapManager.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::osnap::OSnapManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
    return _id;
}

// SIGNAL 0
void aicad::osnap::OSnapManager::snapLocked(const SnapCandidate & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::osnap::OSnapManager::snapCleared()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void aicad::osnap::OSnapManager::settingsChanged(const OSnapSettings & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::osnap::OSnapManager::snapConfirmed(const gp_Pnt & _t1, SnapType _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
