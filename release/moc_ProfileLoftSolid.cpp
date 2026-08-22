/****************************************************************************
** Meta object code from reading C++ file 'ProfileLoftSolid.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/ProfileLoftSolid.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ProfileLoftSolid.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__ProfileLoftSolid_t {
    QByteArrayData data[1];
    char stringdata0[29];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__ProfileLoftSolid_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__ProfileLoftSolid_t qt_meta_stringdata_aicad__cad__ProfileLoftSolid = {
    {
QT_MOC_LITERAL(0, 0, 28) // "aicad::cad::ProfileLoftSolid"

    },
    "aicad::cad::ProfileLoftSolid"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__ProfileLoftSolid[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

void aicad::cad::ProfileLoftSolid::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::ProfileLoftSolid::staticMetaObject = { {
    QMetaObject::SuperData::link<Feature::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__ProfileLoftSolid.data,
    qt_meta_data_aicad__cad__ProfileLoftSolid,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::ProfileLoftSolid::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::ProfileLoftSolid::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__ProfileLoftSolid.stringdata0))
        return static_cast<void*>(this);
    return Feature::qt_metacast(_clname);
}

int aicad::cad::ProfileLoftSolid::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Feature::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
