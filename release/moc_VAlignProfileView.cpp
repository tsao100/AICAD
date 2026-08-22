/****************************************************************************
** Meta object code from reading C++ file 'VAlignProfileView.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/ui/VAlignProfileView.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'VAlignProfileView.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__ui__VAlignProfileView_t {
    QByteArrayData data[11];
    char stringdata0[118];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__ui__VAlignProfileView_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__ui__VAlignProfileView_t qt_meta_stringdata_aicad__ui__VAlignProfileView = {
    {
QT_MOC_LITERAL(0, 0, 28), // "aicad::ui::VAlignProfileView"
QT_MOC_LITERAL(1, 29, 11), // "vipSelected"
QT_MOC_LITERAL(2, 41, 0), // ""
QT_MOC_LITERAL(3, 42, 5), // "index"
QT_MOC_LITERAL(4, 48, 8), // "vipMoved"
QT_MOC_LITERAL(5, 57, 2), // "ch"
QT_MOC_LITERAL(6, 60, 2), // "el"
QT_MOC_LITERAL(7, 63, 8), // "vipAdded"
QT_MOC_LITERAL(8, 72, 18), // "vipDeleteRequested"
QT_MOC_LITERAL(9, 91, 14), // "vipListChanged"
QT_MOC_LITERAL(10, 106, 11) // "cursorMoved"

    },
    "aicad::ui::VAlignProfileView\0vipSelected\0"
    "\0index\0vipMoved\0ch\0el\0vipAdded\0"
    "vipDeleteRequested\0vipListChanged\0"
    "cursorMoved"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__ui__VAlignProfileView[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       6,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   44,    2, 0x06 /* Public */,
       4,    3,   47,    2, 0x06 /* Public */,
       7,    2,   54,    2, 0x06 /* Public */,
       8,    1,   59,    2, 0x06 /* Public */,
       9,    0,   62,    2, 0x06 /* Public */,
      10,    2,   63,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int, QMetaType::Double, QMetaType::Double,    3,    5,    6,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,    5,    6,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,    5,    6,

       0        // eod
};

void aicad::ui::VAlignProfileView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<VAlignProfileView *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->vipSelected((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->vipMoved((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3]))); break;
        case 2: _t->vipAdded((*reinterpret_cast< double(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2]))); break;
        case 3: _t->vipDeleteRequested((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 4: _t->vipListChanged(); break;
        case 5: _t->cursorMoved((*reinterpret_cast< double(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (VAlignProfileView::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignProfileView::vipSelected)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (VAlignProfileView::*)(int , double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignProfileView::vipMoved)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (VAlignProfileView::*)(double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignProfileView::vipAdded)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (VAlignProfileView::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignProfileView::vipDeleteRequested)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (VAlignProfileView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignProfileView::vipListChanged)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (VAlignProfileView::*)(double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VAlignProfileView::cursorMoved)) {
                *result = 5;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::ui::VAlignProfileView::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_aicad__ui__VAlignProfileView.data,
    qt_meta_data_aicad__ui__VAlignProfileView,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::ui::VAlignProfileView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::ui::VAlignProfileView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__ui__VAlignProfileView.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int aicad::ui::VAlignProfileView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
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
void aicad::ui::VAlignProfileView::vipSelected(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::ui::VAlignProfileView::vipMoved(int _t1, double _t2, double _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::ui::VAlignProfileView::vipAdded(double _t1, double _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::ui::VAlignProfileView::vipDeleteRequested(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::ui::VAlignProfileView::vipListChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void aicad::ui::VAlignProfileView::cursorMoved(double _t1, double _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
