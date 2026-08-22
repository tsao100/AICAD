/****************************************************************************
** Meta object code from reading C++ file 'GripManager.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/grips/GripManager.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'GripManager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__GripManager_t {
    QByteArrayData data[13];
    char stringdata0[136];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__GripManager_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__GripManager_t qt_meta_stringdata_aicad__cad__GripManager = {
    {
QT_MOC_LITERAL(0, 0, 23), // "aicad::cad::GripManager"
QT_MOC_LITERAL(1, 24, 15), // "gripDragStarted"
QT_MOC_LITERAL(2, 40, 0), // ""
QT_MOC_LITERAL(3, 41, 6), // "gripId"
QT_MOC_LITERAL(4, 48, 12), // "gripDragging"
QT_MOC_LITERAL(5, 61, 6), // "gp_Pnt"
QT_MOC_LITERAL(6, 68, 3), // "pos"
QT_MOC_LITERAL(7, 72, 16), // "gripDragFinished"
QT_MOC_LITERAL(8, 89, 8), // "startPos"
QT_MOC_LITERAL(9, 98, 6), // "endPos"
QT_MOC_LITERAL(10, 105, 12), // "snapOccurred"
QT_MOC_LITERAL(11, 118, 10), // "SnapResult"
QT_MOC_LITERAL(12, 129, 6) // "result"

    },
    "aicad::cad::GripManager\0gripDragStarted\0"
    "\0gripId\0gripDragging\0gp_Pnt\0pos\0"
    "gripDragFinished\0startPos\0endPos\0"
    "snapOccurred\0SnapResult\0result"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__GripManager[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   34,    2, 0x06 /* Public */,
       4,    2,   37,    2, 0x06 /* Public */,
       7,    3,   42,    2, 0x06 /* Public */,
      10,    1,   49,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 5,    3,    6,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 5, 0x80000000 | 5,    3,    8,    9,
    QMetaType::Void, 0x80000000 | 11,   12,

       0        // eod
};

void aicad::cad::GripManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<GripManager *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->gripDragStarted((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->gripDragging((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const gp_Pnt(*)>(_a[2]))); break;
        case 2: _t->gripDragFinished((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const gp_Pnt(*)>(_a[2])),(*reinterpret_cast< const gp_Pnt(*)>(_a[3]))); break;
        case 3: _t->snapOccurred((*reinterpret_cast< const SnapResult(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (GripManager::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&GripManager::gripDragStarted)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (GripManager::*)(const QString & , const gp_Pnt & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&GripManager::gripDragging)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (GripManager::*)(const QString & , const gp_Pnt & , const gp_Pnt & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&GripManager::gripDragFinished)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (GripManager::*)(const SnapResult & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&GripManager::snapOccurred)) {
                *result = 3;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::GripManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__GripManager.data,
    qt_meta_data_aicad__cad__GripManager,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::GripManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::GripManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__GripManager.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::cad::GripManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
void aicad::cad::GripManager::gripDragStarted(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::cad::GripManager::gripDragging(const QString & _t1, const gp_Pnt & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::cad::GripManager::gripDragFinished(const QString & _t1, const gp_Pnt & _t2, const gp_Pnt & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::cad::GripManager::snapOccurred(const SnapResult & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
