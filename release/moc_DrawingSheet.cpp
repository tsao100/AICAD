/****************************************************************************
** Meta object code from reading C++ file 'DrawingSheet.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/drawing/DrawingSheet.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DrawingSheet.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__drawing__DrawingView_t {
    QByteArrayData data[10];
    char stringdata0[118];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__DrawingView_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__DrawingView_t qt_meta_stringdata_aicad__drawing__DrawingView = {
    {
QT_MOC_LITERAL(0, 0, 27), // "aicad::drawing::DrawingView"
QT_MOC_LITERAL(1, 28, 12), // "labelChanged"
QT_MOC_LITERAL(2, 41, 0), // ""
QT_MOC_LITERAL(3, 42, 5), // "label"
QT_MOC_LITERAL(4, 48, 15), // "positionChanged"
QT_MOC_LITERAL(5, 64, 3), // "pos"
QT_MOC_LITERAL(6, 68, 12), // "scaleChanged"
QT_MOC_LITERAL(7, 81, 5), // "scale"
QT_MOC_LITERAL(8, 87, 13), // "paramsChanged"
QT_MOC_LITERAL(9, 101, 16) // "rebuildRequested"

    },
    "aicad::drawing::DrawingView\0labelChanged\0"
    "\0label\0positionChanged\0pos\0scaleChanged\0"
    "scale\0paramsChanged\0rebuildRequested"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__DrawingView[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   39,    2, 0x06 /* Public */,
       4,    1,   42,    2, 0x06 /* Public */,
       6,    1,   45,    2, 0x06 /* Public */,
       8,    0,   48,    2, 0x06 /* Public */,
       9,    0,   49,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QPointF,    5,
    QMetaType::Void, QMetaType::Double,    7,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void aicad::drawing::DrawingView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DrawingView *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->labelChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->positionChanged((*reinterpret_cast< const QPointF(*)>(_a[1]))); break;
        case 2: _t->scaleChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 3: _t->paramsChanged(); break;
        case 4: _t->rebuildRequested(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (DrawingView::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingView::labelChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (DrawingView::*)(const QPointF & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingView::positionChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (DrawingView::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingView::scaleChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (DrawingView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingView::paramsChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (DrawingView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingView::rebuildRequested)) {
                *result = 4;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::DrawingView::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__DrawingView.data,
    qt_meta_data_aicad__drawing__DrawingView,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::DrawingView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::DrawingView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__DrawingView.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::drawing::DrawingView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void aicad::drawing::DrawingView::labelChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::drawing::DrawingView::positionChanged(const QPointF & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::drawing::DrawingView::scaleChanged(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::drawing::DrawingView::paramsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void aicad::drawing::DrawingView::rebuildRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
struct qt_meta_stringdata_aicad__drawing__PartsListTable_t {
    QByteArrayData data[4];
    char stringdata0[58];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__PartsListTable_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__PartsListTable_t qt_meta_stringdata_aicad__drawing__PartsListTable = {
    {
QT_MOC_LITERAL(0, 0, 30), // "aicad::drawing::PartsListTable"
QT_MOC_LITERAL(1, 31, 11), // "rowsChanged"
QT_MOC_LITERAL(2, 43, 0), // ""
QT_MOC_LITERAL(3, 44, 13) // "configChanged"

    },
    "aicad::drawing::PartsListTable\0"
    "rowsChanged\0\0configChanged"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__PartsListTable[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   24,    2, 0x06 /* Public */,
       3,    0,   25,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void aicad::drawing::PartsListTable::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<PartsListTable *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->rowsChanged(); break;
        case 1: _t->configChanged(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (PartsListTable::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&PartsListTable::rowsChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (PartsListTable::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&PartsListTable::configChanged)) {
                *result = 1;
                return;
            }
        }
    }
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::PartsListTable::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__PartsListTable.data,
    qt_meta_data_aicad__drawing__PartsListTable,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::PartsListTable::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::PartsListTable::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__PartsListTable.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::drawing::PartsListTable::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void aicad::drawing::PartsListTable::rowsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void aicad::drawing::PartsListTable::configChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
struct qt_meta_stringdata_aicad__drawing__RevisionTable_t {
    QByteArrayData data[3];
    char stringdata0[46];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__RevisionTable_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__RevisionTable_t qt_meta_stringdata_aicad__drawing__RevisionTable = {
    {
QT_MOC_LITERAL(0, 0, 29), // "aicad::drawing::RevisionTable"
QT_MOC_LITERAL(1, 30, 14), // "entriesChanged"
QT_MOC_LITERAL(2, 45, 0) // ""

    },
    "aicad::drawing::RevisionTable\0"
    "entriesChanged\0"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__RevisionTable[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   19,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,

       0        // eod
};

void aicad::drawing::RevisionTable::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<RevisionTable *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->entriesChanged(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (RevisionTable::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&RevisionTable::entriesChanged)) {
                *result = 0;
                return;
            }
        }
    }
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::RevisionTable::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__RevisionTable.data,
    qt_meta_data_aicad__drawing__RevisionTable,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::RevisionTable::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::RevisionTable::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__RevisionTable.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::drawing::RevisionTable::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void aicad::drawing::RevisionTable::entriesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
struct qt_meta_stringdata_aicad__drawing__DrawingIndex_t {
    QByteArrayData data[3];
    char stringdata0[45];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__DrawingIndex_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__DrawingIndex_t qt_meta_stringdata_aicad__drawing__DrawingIndex = {
    {
QT_MOC_LITERAL(0, 0, 28), // "aicad::drawing::DrawingIndex"
QT_MOC_LITERAL(1, 29, 14), // "entriesChanged"
QT_MOC_LITERAL(2, 44, 0) // ""

    },
    "aicad::drawing::DrawingIndex\0"
    "entriesChanged\0"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__DrawingIndex[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   19,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,

       0        // eod
};

void aicad::drawing::DrawingIndex::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DrawingIndex *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->entriesChanged(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (DrawingIndex::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingIndex::entriesChanged)) {
                *result = 0;
                return;
            }
        }
    }
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::DrawingIndex::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__DrawingIndex.data,
    qt_meta_data_aicad__drawing__DrawingIndex,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::DrawingIndex::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::DrawingIndex::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__DrawingIndex.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::drawing::DrawingIndex::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void aicad::drawing::DrawingIndex::entriesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
struct qt_meta_stringdata_aicad__drawing__Legend_t {
    QByteArrayData data[3];
    char stringdata0[39];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__Legend_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__Legend_t qt_meta_stringdata_aicad__drawing__Legend = {
    {
QT_MOC_LITERAL(0, 0, 22), // "aicad::drawing::Legend"
QT_MOC_LITERAL(1, 23, 14), // "entriesChanged"
QT_MOC_LITERAL(2, 38, 0) // ""

    },
    "aicad::drawing::Legend\0entriesChanged\0"
    ""
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__Legend[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   19,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,

       0        // eod
};

void aicad::drawing::Legend::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Legend *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->entriesChanged(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Legend::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Legend::entriesChanged)) {
                *result = 0;
                return;
            }
        }
    }
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::Legend::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__Legend.data,
    qt_meta_data_aicad__drawing__Legend,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::Legend::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::Legend::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__Legend.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::drawing::Legend::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void aicad::drawing::Legend::entriesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
struct qt_meta_stringdata_aicad__drawing__DrawingSheet_t {
    QByteArrayData data[17];
    char stringdata0[232];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__DrawingSheet_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__DrawingSheet_t qt_meta_stringdata_aicad__drawing__DrawingSheet = {
    {
QT_MOC_LITERAL(0, 0, 28), // "aicad::drawing::DrawingSheet"
QT_MOC_LITERAL(1, 29, 15), // "fileNameChanged"
QT_MOC_LITERAL(2, 45, 0), // ""
QT_MOC_LITERAL(3, 46, 8), // "fileName"
QT_MOC_LITERAL(4, 55, 15), // "modifiedChanged"
QT_MOC_LITERAL(5, 71, 8), // "modified"
QT_MOC_LITERAL(6, 80, 13), // "configChanged"
QT_MOC_LITERAL(7, 94, 17), // "titleBlockChanged"
QT_MOC_LITERAL(8, 112, 9), // "viewAdded"
QT_MOC_LITERAL(9, 122, 12), // "DrawingView*"
QT_MOC_LITERAL(10, 135, 4), // "view"
QT_MOC_LITERAL(11, 140, 11), // "viewRemoved"
QT_MOC_LITERAL(12, 152, 6), // "viewId"
QT_MOC_LITERAL(13, 159, 18), // "viewsLayoutChanged"
QT_MOC_LITERAL(14, 178, 16), // "partsListChanged"
QT_MOC_LITERAL(15, 195, 20), // "revisionTableChanged"
QT_MOC_LITERAL(16, 216, 15) // "rebuildAllViews"

    },
    "aicad::drawing::DrawingSheet\0"
    "fileNameChanged\0\0fileName\0modifiedChanged\0"
    "modified\0configChanged\0titleBlockChanged\0"
    "viewAdded\0DrawingView*\0view\0viewRemoved\0"
    "viewId\0viewsLayoutChanged\0partsListChanged\0"
    "revisionTableChanged\0rebuildAllViews"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__DrawingSheet[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      10,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      10,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   64,    2, 0x06 /* Public */,
       4,    1,   67,    2, 0x06 /* Public */,
       6,    0,   70,    2, 0x06 /* Public */,
       7,    0,   71,    2, 0x06 /* Public */,
       8,    1,   72,    2, 0x06 /* Public */,
      11,    1,   75,    2, 0x06 /* Public */,
      13,    0,   78,    2, 0x06 /* Public */,
      14,    0,   79,    2, 0x06 /* Public */,
      15,    0,   80,    2, 0x06 /* Public */,
      16,    0,   81,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Bool,    5,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 9,   10,
    QMetaType::Void, QMetaType::QUuid,   12,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void aicad::drawing::DrawingSheet::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DrawingSheet *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->fileNameChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->modifiedChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->configChanged(); break;
        case 3: _t->titleBlockChanged(); break;
        case 4: _t->viewAdded((*reinterpret_cast< DrawingView*(*)>(_a[1]))); break;
        case 5: _t->viewRemoved((*reinterpret_cast< const QUuid(*)>(_a[1]))); break;
        case 6: _t->viewsLayoutChanged(); break;
        case 7: _t->partsListChanged(); break;
        case 8: _t->revisionTableChanged(); break;
        case 9: _t->rebuildAllViews(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DrawingView* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (DrawingSheet::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::fileNameChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::modifiedChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::configChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::titleBlockChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)(DrawingView * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::viewAdded)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)(const QUuid & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::viewRemoved)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::viewsLayoutChanged)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::partsListChanged)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::revisionTableChanged)) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (DrawingSheet::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheet::rebuildAllViews)) {
                *result = 9;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::DrawingSheet::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__DrawingSheet.data,
    qt_meta_data_aicad__drawing__DrawingSheet,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::DrawingSheet::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::DrawingSheet::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__DrawingSheet.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::drawing::DrawingSheet::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void aicad::drawing::DrawingSheet::fileNameChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::drawing::DrawingSheet::modifiedChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::drawing::DrawingSheet::configChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void aicad::drawing::DrawingSheet::titleBlockChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void aicad::drawing::DrawingSheet::viewAdded(DrawingView * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void aicad::drawing::DrawingSheet::viewRemoved(const QUuid & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void aicad::drawing::DrawingSheet::viewsLayoutChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void aicad::drawing::DrawingSheet::partsListChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void aicad::drawing::DrawingSheet::revisionTableChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void aicad::drawing::DrawingSheet::rebuildAllViews()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
