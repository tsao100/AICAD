/****************************************************************************
** Meta object code from reading C++ file 'ConstraintPickSession.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/ConstraintPickSession.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ConstraintPickSession.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__ConstraintPickSession_t {
    QByteArrayData data[13];
    char stringdata0[147];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__ConstraintPickSession_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__ConstraintPickSession_t qt_meta_stringdata_aicad__cad__ConstraintPickSession = {
    {
QT_MOC_LITERAL(0, 0, 33), // "aicad::cad::ConstraintPickSes..."
QT_MOC_LITERAL(1, 34, 15), // "constraintReady"
QT_MOC_LITERAL(2, 50, 0), // ""
QT_MOC_LITERAL(3, 51, 14), // "QList<GeomRef>"
QT_MOC_LITERAL(4, 66, 4), // "refs"
QT_MOC_LITERAL(5, 71, 5), // "value"
QT_MOC_LITERAL(6, 77, 9), // "paramExpr"
QT_MOC_LITERAL(7, 87, 7), // "driving"
QT_MOC_LITERAL(8, 95, 14), // "ConstraintType"
QT_MOC_LITERAL(9, 110, 4), // "type"
QT_MOC_LITERAL(10, 115, 13), // "promptChanged"
QT_MOC_LITERAL(11, 129, 4), // "text"
QT_MOC_LITERAL(12, 134, 12) // "sessionEnded"

    },
    "aicad::cad::ConstraintPickSession\0"
    "constraintReady\0\0QList<GeomRef>\0refs\0"
    "value\0paramExpr\0driving\0ConstraintType\0"
    "type\0promptChanged\0text\0sessionEnded"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__ConstraintPickSession[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    5,   29,    2, 0x06 /* Public */,
      10,    1,   40,    2, 0x06 /* Public */,
      12,    0,   43,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Double, QMetaType::QString, QMetaType::Bool, 0x80000000 | 8,    4,    5,    6,    7,    9,
    QMetaType::Void, QMetaType::QString,   11,
    QMetaType::Void,

       0        // eod
};

void aicad::cad::ConstraintPickSession::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ConstraintPickSession *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->constraintReady((*reinterpret_cast< QList<GeomRef>(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2])),(*reinterpret_cast< QString(*)>(_a[3])),(*reinterpret_cast< bool(*)>(_a[4])),(*reinterpret_cast< ConstraintType(*)>(_a[5]))); break;
        case 1: _t->promptChanged((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 2: _t->sessionEnded(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ConstraintPickSession::*)(QList<GeomRef> , double , QString , bool , ConstraintType );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ConstraintPickSession::constraintReady)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ConstraintPickSession::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ConstraintPickSession::promptChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ConstraintPickSession::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ConstraintPickSession::sessionEnded)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::ConstraintPickSession::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__ConstraintPickSession.data,
    qt_meta_data_aicad__cad__ConstraintPickSession,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::ConstraintPickSession::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::ConstraintPickSession::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__ConstraintPickSession.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::cad::ConstraintPickSession::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
    return _id;
}

// SIGNAL 0
void aicad::cad::ConstraintPickSession::constraintReady(QList<GeomRef> _t1, double _t2, QString _t3, bool _t4, ConstraintType _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::cad::ConstraintPickSession::promptChanged(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::cad::ConstraintPickSession::sessionEnded()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
