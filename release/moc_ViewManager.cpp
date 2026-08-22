/****************************************************************************
** Meta object code from reading C++ file 'ViewManager.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/view/ViewManager.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ViewManager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__view__ViewManager_t {
    QByteArrayData data[10];
    char stringdata0[114];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__view__ViewManager_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__view__ViewManager_t qt_meta_stringdata_aicad__view__ViewManager = {
    {
QT_MOC_LITERAL(0, 0, 24), // "aicad::view::ViewManager"
QT_MOC_LITERAL(1, 25, 11), // "viewCreated"
QT_MOC_LITERAL(2, 37, 0), // ""
QT_MOC_LITERAL(3, 38, 8), // "CadView*"
QT_MOC_LITERAL(4, 47, 4), // "view"
QT_MOC_LITERAL(5, 52, 10), // "viewClosed"
QT_MOC_LITERAL(6, 63, 17), // "activeViewChanged"
QT_MOC_LITERAL(7, 81, 16), // "viewCountChanged"
QT_MOC_LITERAL(8, 98, 5), // "count"
QT_MOC_LITERAL(9, 104, 9) // "viewCount"

    },
    "aicad::view::ViewManager\0viewCreated\0"
    "\0CadView*\0view\0viewClosed\0activeViewChanged\0"
    "viewCountChanged\0count\0viewCount"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__view__ViewManager[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       1,   46, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   34,    2, 0x06 /* Public */,
       5,    1,   37,    2, 0x06 /* Public */,
       6,    1,   40,    2, 0x06 /* Public */,
       7,    1,   43,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::Int,    8,

 // properties: name, type, flags
       9, QMetaType::Int, 0x00495001,

 // properties: notify_signal_id
       3,

       0        // eod
};

void aicad::view::ViewManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ViewManager *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->viewCreated((*reinterpret_cast< CadView*(*)>(_a[1]))); break;
        case 1: _t->viewClosed((*reinterpret_cast< CadView*(*)>(_a[1]))); break;
        case 2: _t->activeViewChanged((*reinterpret_cast< CadView*(*)>(_a[1]))); break;
        case 3: _t->viewCountChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ViewManager::*)(CadView * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ViewManager::viewCreated)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ViewManager::*)(CadView * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ViewManager::viewClosed)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ViewManager::*)(CadView * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ViewManager::activeViewChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (ViewManager::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ViewManager::viewCountChanged)) {
                *result = 3;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<ViewManager *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->viewCount(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
}

QT_INIT_METAOBJECT const QMetaObject aicad::view::ViewManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__view__ViewManager.data,
    qt_meta_data_aicad__view__ViewManager,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::view::ViewManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::view::ViewManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__view__ViewManager.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::view::ViewManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 1;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void aicad::view::ViewManager::viewCreated(CadView * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::view::ViewManager::viewClosed(CadView * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::view::ViewManager::activeViewChanged(CadView * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::view::ViewManager::viewCountChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
