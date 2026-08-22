/****************************************************************************
** Meta object code from reading C++ file 'VAlignCommandBar.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/ui/VAlignCommandBar.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'VAlignCommandBar.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__ui__VAlignCommandBar_t {
    QByteArrayData data[15];
    char stringdata0[134];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__ui__VAlignCommandBar_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__ui__VAlignCommandBar_t qt_meta_stringdata_aicad__ui__VAlignCommandBar = {
    {
QT_MOC_LITERAL(0, 0, 27), // "aicad::ui::VAlignCommandBar"
QT_MOC_LITERAL(1, 28, 14), // "commandEntered"
QT_MOC_LITERAL(2, 43, 0), // ""
QT_MOC_LITERAL(3, 44, 4), // "text"
QT_MOC_LITERAL(4, 49, 11), // "chainageSet"
QT_MOC_LITERAL(5, 61, 2), // "ch"
QT_MOC_LITERAL(6, 64, 12), // "elevationSet"
QT_MOC_LITERAL(7, 77, 2), // "el"
QT_MOC_LITERAL(8, 80, 9), // "kValueSet"
QT_MOC_LITERAL(9, 90, 1), // "K"
QT_MOC_LITERAL(10, 92, 6), // "lvcSet"
QT_MOC_LITERAL(11, 99, 3), // "lvc"
QT_MOC_LITERAL(12, 103, 8), // "gradeSet"
QT_MOC_LITERAL(13, 112, 5), // "grade"
QT_MOC_LITERAL(14, 118, 15) // "onReturnPressed"

    },
    "aicad::ui::VAlignCommandBar\0commandEntered\0"
    "\0text\0chainageSet\0ch\0elevationSet\0el\0"
    "kValueSet\0K\0lvcSet\0lvc\0gradeSet\0grade\0"
    "onReturnPressed"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__ui__VAlignCommandBar[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       6,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   49,    2, 0x06 /* Public */,
       4,    1,   52,    2, 0x06 /* Public */,
       6,    1,   55,    2, 0x06 /* Public */,
       8,    1,   58,    2, 0x06 /* Public */,
      10,    1,   61,    2, 0x06 /* Public */,
      12,    1,   64,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      14,    0,   67,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Double,    5,
    QMetaType::Void, QMetaType::Double,    7,
    QMetaType::Void, QMetaType::Double,    9,
    QMetaType::Void, QMetaType::Double,   11,
    QMetaType::Void, QMetaType::Double,   13,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

void aicad::ui::VAlignCommandBar::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<VAlignCommandBar *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->commandEntered((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->chainageSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 2: _t->elevationSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 3: _t->kValueSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 4: _t->lvcSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 5: _t->gradeSet((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 6: _t->onReturnPressed(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (VAlignCommandBar::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignCommandBar::commandEntered)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (VAlignCommandBar::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignCommandBar::chainageSet)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (VAlignCommandBar::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignCommandBar::elevationSet)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (VAlignCommandBar::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignCommandBar::kValueSet)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (VAlignCommandBar::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignCommandBar::lvcSet)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (VAlignCommandBar::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignCommandBar::gradeSet)) {
                *result = 5;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::ui::VAlignCommandBar::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_aicad__ui__VAlignCommandBar.data,
    qt_meta_data_aicad__ui__VAlignCommandBar,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::ui::VAlignCommandBar::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::ui::VAlignCommandBar::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__ui__VAlignCommandBar.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int aicad::ui::VAlignCommandBar::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
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
    return _id;
}

// SIGNAL 0
void aicad::ui::VAlignCommandBar::commandEntered(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::ui::VAlignCommandBar::chainageSet(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::ui::VAlignCommandBar::elevationSet(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::ui::VAlignCommandBar::kValueSet(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::ui::VAlignCommandBar::lvcSet(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void aicad::ui::VAlignCommandBar::gradeSet(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
