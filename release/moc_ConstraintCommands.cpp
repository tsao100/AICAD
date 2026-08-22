/****************************************************************************
** Meta object code from reading C++ file 'ConstraintCommands.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/command/ConstraintCommands.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ConstraintCommands.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__command__GeomConstraintCommand_t {
    QByteArrayData data[1];
    char stringdata0[38];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__GeomConstraintCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__GeomConstraintCommand_t qt_meta_stringdata_aicad__command__GeomConstraintCommand = {
    {
QT_MOC_LITERAL(0, 0, 37) // "aicad::command::GeomConstrain..."

    },
    "aicad::command::GeomConstraintCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__GeomConstraintCommand[] = {

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

void aicad::command::GeomConstraintCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::GeomConstraintCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__GeomConstraintCommand.data,
    qt_meta_data_aicad__command__GeomConstraintCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::GeomConstraintCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::GeomConstraintCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__GeomConstraintCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::GeomConstraintCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__CoincidentCommand_t {
    QByteArrayData data[1];
    char stringdata0[34];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__CoincidentCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__CoincidentCommand_t qt_meta_stringdata_aicad__command__CoincidentCommand = {
    {
QT_MOC_LITERAL(0, 0, 33) // "aicad::command::CoincidentCom..."

    },
    "aicad::command::CoincidentCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__CoincidentCommand[] = {

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

void aicad::command::CoincidentCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::CoincidentCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__CoincidentCommand.data,
    qt_meta_data_aicad__command__CoincidentCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::CoincidentCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::CoincidentCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__CoincidentCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::CoincidentCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__HorizontalCommand_t {
    QByteArrayData data[1];
    char stringdata0[34];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__HorizontalCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__HorizontalCommand_t qt_meta_stringdata_aicad__command__HorizontalCommand = {
    {
QT_MOC_LITERAL(0, 0, 33) // "aicad::command::HorizontalCom..."

    },
    "aicad::command::HorizontalCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__HorizontalCommand[] = {

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

void aicad::command::HorizontalCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::HorizontalCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__HorizontalCommand.data,
    qt_meta_data_aicad__command__HorizontalCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::HorizontalCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::HorizontalCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__HorizontalCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::HorizontalCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__VerticalCommand_t {
    QByteArrayData data[1];
    char stringdata0[32];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__VerticalCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__VerticalCommand_t qt_meta_stringdata_aicad__command__VerticalCommand = {
    {
QT_MOC_LITERAL(0, 0, 31) // "aicad::command::VerticalCommand"

    },
    "aicad::command::VerticalCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__VerticalCommand[] = {

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

void aicad::command::VerticalCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::VerticalCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__VerticalCommand.data,
    qt_meta_data_aicad__command__VerticalCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::VerticalCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::VerticalCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__VerticalCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::VerticalCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__ParallelCommand_t {
    QByteArrayData data[1];
    char stringdata0[32];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__ParallelCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__ParallelCommand_t qt_meta_stringdata_aicad__command__ParallelCommand = {
    {
QT_MOC_LITERAL(0, 0, 31) // "aicad::command::ParallelCommand"

    },
    "aicad::command::ParallelCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__ParallelCommand[] = {

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

void aicad::command::ParallelCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::ParallelCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__ParallelCommand.data,
    qt_meta_data_aicad__command__ParallelCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::ParallelCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::ParallelCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__ParallelCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::ParallelCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__PerpendicularCommand_t {
    QByteArrayData data[1];
    char stringdata0[37];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__PerpendicularCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__PerpendicularCommand_t qt_meta_stringdata_aicad__command__PerpendicularCommand = {
    {
QT_MOC_LITERAL(0, 0, 36) // "aicad::command::Perpendicular..."

    },
    "aicad::command::PerpendicularCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__PerpendicularCommand[] = {

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

void aicad::command::PerpendicularCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::PerpendicularCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__PerpendicularCommand.data,
    qt_meta_data_aicad__command__PerpendicularCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::PerpendicularCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::PerpendicularCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__PerpendicularCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::PerpendicularCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__TangentCommand_t {
    QByteArrayData data[1];
    char stringdata0[31];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__TangentCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__TangentCommand_t qt_meta_stringdata_aicad__command__TangentCommand = {
    {
QT_MOC_LITERAL(0, 0, 30) // "aicad::command::TangentCommand"

    },
    "aicad::command::TangentCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__TangentCommand[] = {

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

void aicad::command::TangentCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::TangentCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__TangentCommand.data,
    qt_meta_data_aicad__command__TangentCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::TangentCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::TangentCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__TangentCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::TangentCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__ConcentricCommand_t {
    QByteArrayData data[1];
    char stringdata0[34];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__ConcentricCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__ConcentricCommand_t qt_meta_stringdata_aicad__command__ConcentricCommand = {
    {
QT_MOC_LITERAL(0, 0, 33) // "aicad::command::ConcentricCom..."

    },
    "aicad::command::ConcentricCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__ConcentricCommand[] = {

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

void aicad::command::ConcentricCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::ConcentricCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__ConcentricCommand.data,
    qt_meta_data_aicad__command__ConcentricCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::ConcentricCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::ConcentricCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__ConcentricCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::ConcentricCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__EqualLenCommand_t {
    QByteArrayData data[1];
    char stringdata0[32];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__EqualLenCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__EqualLenCommand_t qt_meta_stringdata_aicad__command__EqualLenCommand = {
    {
QT_MOC_LITERAL(0, 0, 31) // "aicad::command::EqualLenCommand"

    },
    "aicad::command::EqualLenCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__EqualLenCommand[] = {

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

void aicad::command::EqualLenCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::EqualLenCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__EqualLenCommand.data,
    qt_meta_data_aicad__command__EqualLenCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::EqualLenCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::EqualLenCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__EqualLenCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::EqualLenCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__EqualRadCommand_t {
    QByteArrayData data[1];
    char stringdata0[32];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__EqualRadCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__EqualRadCommand_t qt_meta_stringdata_aicad__command__EqualRadCommand = {
    {
QT_MOC_LITERAL(0, 0, 31) // "aicad::command::EqualRadCommand"

    },
    "aicad::command::EqualRadCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__EqualRadCommand[] = {

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

void aicad::command::EqualRadCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::EqualRadCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__EqualRadCommand.data,
    qt_meta_data_aicad__command__EqualRadCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::EqualRadCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::EqualRadCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__EqualRadCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::EqualRadCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__CollinearCommand_t {
    QByteArrayData data[1];
    char stringdata0[33];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__CollinearCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__CollinearCommand_t qt_meta_stringdata_aicad__command__CollinearCommand = {
    {
QT_MOC_LITERAL(0, 0, 32) // "aicad::command::CollinearCommand"

    },
    "aicad::command::CollinearCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__CollinearCommand[] = {

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

void aicad::command::CollinearCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::CollinearCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__CollinearCommand.data,
    qt_meta_data_aicad__command__CollinearCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::CollinearCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::CollinearCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__CollinearCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::CollinearCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__MidpointCommand_t {
    QByteArrayData data[1];
    char stringdata0[32];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__MidpointCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__MidpointCommand_t qt_meta_stringdata_aicad__command__MidpointCommand = {
    {
QT_MOC_LITERAL(0, 0, 31) // "aicad::command::MidpointCommand"

    },
    "aicad::command::MidpointCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__MidpointCommand[] = {

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

void aicad::command::MidpointCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::MidpointCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__MidpointCommand.data,
    qt_meta_data_aicad__command__MidpointCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::MidpointCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::MidpointCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__MidpointCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::MidpointCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__SymmetricCommand_t {
    QByteArrayData data[1];
    char stringdata0[33];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__SymmetricCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__SymmetricCommand_t qt_meta_stringdata_aicad__command__SymmetricCommand = {
    {
QT_MOC_LITERAL(0, 0, 32) // "aicad::command::SymmetricCommand"

    },
    "aicad::command::SymmetricCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__SymmetricCommand[] = {

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

void aicad::command::SymmetricCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::SymmetricCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__SymmetricCommand.data,
    qt_meta_data_aicad__command__SymmetricCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::SymmetricCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::SymmetricCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__SymmetricCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::SymmetricCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__PointOnCurveCommand_t {
    QByteArrayData data[1];
    char stringdata0[36];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__PointOnCurveCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__PointOnCurveCommand_t qt_meta_stringdata_aicad__command__PointOnCurveCommand = {
    {
QT_MOC_LITERAL(0, 0, 35) // "aicad::command::PointOnCurveC..."

    },
    "aicad::command::PointOnCurveCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__PointOnCurveCommand[] = {

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

void aicad::command::PointOnCurveCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::PointOnCurveCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__PointOnCurveCommand.data,
    qt_meta_data_aicad__command__PointOnCurveCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::PointOnCurveCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::PointOnCurveCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__PointOnCurveCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::PointOnCurveCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__FixCommand_t {
    QByteArrayData data[1];
    char stringdata0[27];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__FixCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__FixCommand_t qt_meta_stringdata_aicad__command__FixCommand = {
    {
QT_MOC_LITERAL(0, 0, 26) // "aicad::command::FixCommand"

    },
    "aicad::command::FixCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__FixCommand[] = {

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

void aicad::command::FixCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::FixCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<GeomConstraintCommand::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__FixCommand.data,
    qt_meta_data_aicad__command__FixCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::FixCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::FixCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__FixCommand.stringdata0))
        return static_cast<void*>(this);
    return GeomConstraintCommand::qt_metacast(_clname);
}

int aicad::command::FixCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = GeomConstraintCommand::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__DimConstraintCommand_t {
    QByteArrayData data[1];
    char stringdata0[37];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__DimConstraintCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__DimConstraintCommand_t qt_meta_stringdata_aicad__command__DimConstraintCommand = {
    {
QT_MOC_LITERAL(0, 0, 36) // "aicad::command::DimConstraint..."

    },
    "aicad::command::DimConstraintCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__DimConstraintCommand[] = {

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

void aicad::command::DimConstraintCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::DimConstraintCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__DimConstraintCommand.data,
    qt_meta_data_aicad__command__DimConstraintCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::DimConstraintCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::DimConstraintCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__DimConstraintCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::DimConstraintCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__DelConCommand_t {
    QByteArrayData data[1];
    char stringdata0[30];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__DelConCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__DelConCommand_t qt_meta_stringdata_aicad__command__DelConCommand = {
    {
QT_MOC_LITERAL(0, 0, 29) // "aicad::command::DelConCommand"

    },
    "aicad::command::DelConCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__DelConCommand[] = {

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

void aicad::command::DelConCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::DelConCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__DelConCommand.data,
    qt_meta_data_aicad__command__DelConCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::DelConCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::DelConCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__DelConCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::DelConCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__EditConCommand_t {
    QByteArrayData data[1];
    char stringdata0[31];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__EditConCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__EditConCommand_t qt_meta_stringdata_aicad__command__EditConCommand = {
    {
QT_MOC_LITERAL(0, 0, 30) // "aicad::command::EditConCommand"

    },
    "aicad::command::EditConCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__EditConCommand[] = {

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

void aicad::command::EditConCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::EditConCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__EditConCommand.data,
    qt_meta_data_aicad__command__EditConCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::EditConCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::EditConCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__EditConCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::EditConCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__ListConCommand_t {
    QByteArrayData data[1];
    char stringdata0[31];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__ListConCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__ListConCommand_t qt_meta_stringdata_aicad__command__ListConCommand = {
    {
QT_MOC_LITERAL(0, 0, 30) // "aicad::command::ListConCommand"

    },
    "aicad::command::ListConCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__ListConCommand[] = {

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

void aicad::command::ListConCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::ListConCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__ListConCommand.data,
    qt_meta_data_aicad__command__ListConCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::ListConCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::ListConCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__ListConCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::ListConCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__ConInfoCommand_t {
    QByteArrayData data[1];
    char stringdata0[31];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__ConInfoCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__ConInfoCommand_t qt_meta_stringdata_aicad__command__ConInfoCommand = {
    {
QT_MOC_LITERAL(0, 0, 30) // "aicad::command::ConInfoCommand"

    },
    "aicad::command::ConInfoCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__ConInfoCommand[] = {

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

void aicad::command::ConInfoCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::ConInfoCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__ConInfoCommand.data,
    qt_meta_data_aicad__command__ConInfoCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::ConInfoCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::ConInfoCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__ConInfoCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::ConInfoCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__SolveCommand_t {
    QByteArrayData data[1];
    char stringdata0[29];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__SolveCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__SolveCommand_t qt_meta_stringdata_aicad__command__SolveCommand = {
    {
QT_MOC_LITERAL(0, 0, 28) // "aicad::command::SolveCommand"

    },
    "aicad::command::SolveCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__SolveCommand[] = {

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

void aicad::command::SolveCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::SolveCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__SolveCommand.data,
    qt_meta_data_aicad__command__SolveCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::SolveCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::SolveCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__SolveCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::SolveCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__DofCommand_t {
    QByteArrayData data[1];
    char stringdata0[27];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__DofCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__DofCommand_t qt_meta_stringdata_aicad__command__DofCommand = {
    {
QT_MOC_LITERAL(0, 0, 26) // "aicad::command::DofCommand"

    },
    "aicad::command::DofCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__DofCommand[] = {

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

void aicad::command::DofCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::DofCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__DofCommand.data,
    qt_meta_data_aicad__command__DofCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::DofCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::DofCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__DofCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::DofCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__ConVisCommand_t {
    QByteArrayData data[1];
    char stringdata0[30];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__ConVisCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__ConVisCommand_t qt_meta_stringdata_aicad__command__ConVisCommand = {
    {
QT_MOC_LITERAL(0, 0, 29) // "aicad::command::ConVisCommand"

    },
    "aicad::command::ConVisCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__ConVisCommand[] = {

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

void aicad::command::ConVisCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::ConVisCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__ConVisCommand.data,
    qt_meta_data_aicad__command__ConVisCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::ConVisCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::ConVisCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__ConVisCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::ConVisCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_aicad__command__ListPointsCommand_t {
    QByteArrayData data[1];
    char stringdata0[34];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__command__ListPointsCommand_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__command__ListPointsCommand_t qt_meta_stringdata_aicad__command__ListPointsCommand = {
    {
QT_MOC_LITERAL(0, 0, 33) // "aicad::command::ListPointsCom..."

    },
    "aicad::command::ListPointsCommand"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__command__ListPointsCommand[] = {

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

void aicad::command::ListPointsCommand::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::command::ListPointsCommand::staticMetaObject = { {
    QMetaObject::SuperData::link<Command::staticMetaObject>(),
    qt_meta_stringdata_aicad__command__ListPointsCommand.data,
    qt_meta_data_aicad__command__ListPointsCommand,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::command::ListPointsCommand::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::command::ListPointsCommand::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__command__ListPointsCommand.stringdata0))
        return static_cast<void*>(this);
    return Command::qt_metacast(_clname);
}

int aicad::command::ListPointsCommand::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Command::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
