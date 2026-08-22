/****************************************************************************
** Meta object code from reading C++ file 'AlignmentDataTableDialog.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/ui/AlignmentDataTableDialog.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AlignmentDataTableDialog.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__ui__AlignmentDataTableDialog_t {
    QByteArrayData data[10];
    char stringdata0[133];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__ui__AlignmentDataTableDialog_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__ui__AlignmentDataTableDialog_t qt_meta_stringdata_aicad__ui__AlignmentDataTableDialog = {
    {
QT_MOC_LITERAL(0, 0, 35), // "aicad::ui::AlignmentDataTable..."
QT_MOC_LITERAL(1, 36, 13), // "dataCommitted"
QT_MOC_LITERAL(2, 50, 0), // ""
QT_MOC_LITERAL(3, 51, 14), // "onHCellChanged"
QT_MOC_LITERAL(4, 66, 17), // "QTableWidgetItem*"
QT_MOC_LITERAL(5, 84, 4), // "item"
QT_MOC_LITERAL(6, 89, 14), // "onVCellChanged"
QT_MOC_LITERAL(7, 104, 20), // "onHCellDoubleClicked"
QT_MOC_LITERAL(8, 125, 3), // "row"
QT_MOC_LITERAL(9, 129, 3) // "col"

    },
    "aicad::ui::AlignmentDataTableDialog\0"
    "dataCommitted\0\0onHCellChanged\0"
    "QTableWidgetItem*\0item\0onVCellChanged\0"
    "onHCellDoubleClicked\0row\0col"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__ui__AlignmentDataTableDialog[] = {

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
       1,    0,   34,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       3,    1,   35,    2, 0x08 /* Private */,
       6,    1,   38,    2, 0x08 /* Private */,
       7,    2,   41,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 4,    5,
    QMetaType::Void, 0x80000000 | 4,    5,
    QMetaType::Void, QMetaType::Int, QMetaType::Int,    8,    9,

       0        // eod
};

void aicad::ui::AlignmentDataTableDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<AlignmentDataTableDialog *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->dataCommitted(); break;
        case 1: _t->onHCellChanged((*reinterpret_cast< QTableWidgetItem*(*)>(_a[1]))); break;
        case 2: _t->onVCellChanged((*reinterpret_cast< QTableWidgetItem*(*)>(_a[1]))); break;
        case 3: _t->onHCellDoubleClicked((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (AlignmentDataTableDialog::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&AlignmentDataTableDialog::dataCommitted)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::ui::AlignmentDataTableDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_meta_stringdata_aicad__ui__AlignmentDataTableDialog.data,
    qt_meta_data_aicad__ui__AlignmentDataTableDialog,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::ui::AlignmentDataTableDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::ui::AlignmentDataTableDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__ui__AlignmentDataTableDialog.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int aicad::ui::AlignmentDataTableDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
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
void aicad::ui::AlignmentDataTableDialog::dataCommitted()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
