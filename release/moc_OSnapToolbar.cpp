/****************************************************************************
** Meta object code from reading C++ file 'OSnapToolbar.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/osnap/OSnapToolbar.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'OSnapToolbar.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__osnap__OSnapToolbar_t {
    QByteArrayData data[17];
    char stringdata0[221];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__osnap__OSnapToolbar_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__osnap__OSnapToolbar_t qt_meta_stringdata_aicad__osnap__OSnapToolbar = {
    {
QT_MOC_LITERAL(0, 0, 26), // "aicad::osnap::OSnapToolbar"
QT_MOC_LITERAL(1, 27, 15), // "snapTypeToggled"
QT_MOC_LITERAL(2, 43, 0), // ""
QT_MOC_LITERAL(3, 44, 8), // "SnapType"
QT_MOC_LITERAL(4, 53, 4), // "type"
QT_MOC_LITERAL(5, 58, 7), // "enabled"
QT_MOC_LITERAL(6, 66, 15), // "onActionToggled"
QT_MOC_LITERAL(7, 82, 7), // "checked"
QT_MOC_LITERAL(8, 90, 12), // "onSnapLocked"
QT_MOC_LITERAL(9, 103, 13), // "SnapCandidate"
QT_MOC_LITERAL(10, 117, 9), // "candidate"
QT_MOC_LITERAL(11, 127, 13), // "onSnapCleared"
QT_MOC_LITERAL(12, 141, 17), // "onSettingsChanged"
QT_MOC_LITERAL(13, 159, 13), // "OSnapSettings"
QT_MOC_LITERAL(14, 173, 8), // "settings"
QT_MOC_LITERAL(15, 182, 18), // "onEnableAllClicked"
QT_MOC_LITERAL(16, 201, 19) // "onDisableAllClicked"

    },
    "aicad::osnap::OSnapToolbar\0snapTypeToggled\0"
    "\0SnapType\0type\0enabled\0onActionToggled\0"
    "checked\0onSnapLocked\0SnapCandidate\0"
    "candidate\0onSnapCleared\0onSettingsChanged\0"
    "OSnapSettings\0settings\0onEnableAllClicked\0"
    "onDisableAllClicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__osnap__OSnapToolbar[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    2,   49,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       6,    1,   54,    2, 0x08 /* Private */,
       8,    1,   57,    2, 0x08 /* Private */,
      11,    0,   60,    2, 0x08 /* Private */,
      12,    1,   61,    2, 0x08 /* Private */,
      15,    0,   64,    2, 0x08 /* Private */,
      16,    0,   65,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Bool,    4,    5,

 // slots: parameters
    QMetaType::Void, QMetaType::Bool,    7,
    QMetaType::Void, 0x80000000 | 9,   10,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 13,   14,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void aicad::osnap::OSnapToolbar::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<OSnapToolbar *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->snapTypeToggled((*reinterpret_cast< SnapType(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 1: _t->onActionToggled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->onSnapLocked((*reinterpret_cast< const SnapCandidate(*)>(_a[1]))); break;
        case 3: _t->onSnapCleared(); break;
        case 4: _t->onSettingsChanged((*reinterpret_cast< const OSnapSettings(*)>(_a[1]))); break;
        case 5: _t->onEnableAllClicked(); break;
        case 6: _t->onDisableAllClicked(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (OSnapToolbar::*)(SnapType , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&OSnapToolbar::snapTypeToggled)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::osnap::OSnapToolbar::staticMetaObject = { {
    QMetaObject::SuperData::link<QToolBar::staticMetaObject>(),
    qt_meta_stringdata_aicad__osnap__OSnapToolbar.data,
    qt_meta_data_aicad__osnap__OSnapToolbar,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::osnap::OSnapToolbar::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::osnap::OSnapToolbar::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__osnap__OSnapToolbar.stringdata0))
        return static_cast<void*>(this);
    return QToolBar::qt_metacast(_clname);
}

int aicad::osnap::OSnapToolbar::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QToolBar::qt_metacall(_c, _id, _a);
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
void aicad::osnap::OSnapToolbar::snapTypeToggled(SnapType _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
