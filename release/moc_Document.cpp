/****************************************************************************
** Meta object code from reading C++ file 'Document.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/cad/Document.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Document.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__cad__Document_t {
    QByteArrayData data[30];
    char stringdata0[486];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__cad__Document_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__cad__Document_t qt_meta_stringdata_aicad__cad__Document = {
    {
QT_MOC_LITERAL(0, 0, 20), // "aicad::cad::Document"
QT_MOC_LITERAL(1, 21, 15), // "fileNameChanged"
QT_MOC_LITERAL(2, 37, 0), // ""
QT_MOC_LITERAL(3, 38, 8), // "fileName"
QT_MOC_LITERAL(4, 47, 15), // "modifiedChanged"
QT_MOC_LITERAL(5, 63, 8), // "modified"
QT_MOC_LITERAL(6, 72, 12), // "featureAdded"
QT_MOC_LITERAL(7, 85, 8), // "Feature*"
QT_MOC_LITERAL(8, 94, 7), // "feature"
QT_MOC_LITERAL(9, 102, 23), // "featureAboutToBeRemoved"
QT_MOC_LITERAL(10, 126, 14), // "featureRemoved"
QT_MOC_LITERAL(11, 141, 19), // "featureCountChanged"
QT_MOC_LITERAL(12, 161, 5), // "count"
QT_MOC_LITERAL(13, 167, 12), // "aboutToClose"
QT_MOC_LITERAL(14, 180, 14), // "rebuildStarted"
QT_MOC_LITERAL(15, 195, 15), // "rebuildFinished"
QT_MOC_LITERAL(16, 211, 7), // "success"
QT_MOC_LITERAL(17, 219, 28), // "referenceGeometryInitialized"
QT_MOC_LITERAL(18, 248, 34), // "referenceGeometryVisibilityCh..."
QT_MOC_LITERAL(19, 283, 21), // "ReferenceGeometryType"
QT_MOC_LITERAL(20, 305, 4), // "type"
QT_MOC_LITERAL(21, 310, 7), // "visible"
QT_MOC_LITERAL(22, 318, 20), // "treeStructureChanged"
QT_MOC_LITERAL(23, 339, 17), // "allFeaturesLoaded"
QT_MOC_LITERAL(24, 357, 28), // "originReinitializationNeeded"
QT_MOC_LITERAL(25, 386, 19), // "featureShapeUpdated"
QT_MOC_LITERAL(26, 406, 23), // "trackCenterLinesChanged"
QT_MOC_LITERAL(27, 430, 25), // "onFeatureRebuildRequested"
QT_MOC_LITERAL(28, 456, 16), // "onFeatureChanged"
QT_MOC_LITERAL(29, 473, 12) // "featureCount"

    },
    "aicad::cad::Document\0fileNameChanged\0"
    "\0fileName\0modifiedChanged\0modified\0"
    "featureAdded\0Feature*\0feature\0"
    "featureAboutToBeRemoved\0featureRemoved\0"
    "featureCountChanged\0count\0aboutToClose\0"
    "rebuildStarted\0rebuildFinished\0success\0"
    "referenceGeometryInitialized\0"
    "referenceGeometryVisibilityChanged\0"
    "ReferenceGeometryType\0type\0visible\0"
    "treeStructureChanged\0allFeaturesLoaded\0"
    "originReinitializationNeeded\0"
    "featureShapeUpdated\0trackCenterLinesChanged\0"
    "onFeatureRebuildRequested\0onFeatureChanged\0"
    "featureCount"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__cad__Document[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      18,   14, // methods
       3,  140, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      16,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,  104,    2, 0x06 /* Public */,
       4,    1,  107,    2, 0x06 /* Public */,
       6,    1,  110,    2, 0x06 /* Public */,
       9,    1,  113,    2, 0x06 /* Public */,
      10,    0,  116,    2, 0x06 /* Public */,
      11,    1,  117,    2, 0x06 /* Public */,
      13,    0,  120,    2, 0x06 /* Public */,
      14,    0,  121,    2, 0x06 /* Public */,
      15,    1,  122,    2, 0x06 /* Public */,
      17,    0,  125,    2, 0x06 /* Public */,
      18,    2,  126,    2, 0x06 /* Public */,
      22,    0,  131,    2, 0x06 /* Public */,
      23,    0,  132,    2, 0x06 /* Public */,
      24,    0,  133,    2, 0x06 /* Public */,
      25,    1,  134,    2, 0x06 /* Public */,
      26,    0,  137,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      27,    0,  138,    2, 0x08 /* Private */,
      28,    0,  139,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Bool,    5,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   12,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   16,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 19, QMetaType::Bool,   20,   21,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,

 // properties: name, type, flags
       3, QMetaType::QString, 0x00495103,
       5, QMetaType::Bool, 0x00495001,
      29, QMetaType::Int, 0x00495001,

 // properties: notify_signal_id
       0,
       1,
       5,

       0        // eod
};

void aicad::cad::Document::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Document *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->fileNameChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->modifiedChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->featureAdded((*reinterpret_cast< Feature*(*)>(_a[1]))); break;
        case 3: _t->featureAboutToBeRemoved((*reinterpret_cast< Feature*(*)>(_a[1]))); break;
        case 4: _t->featureRemoved(); break;
        case 5: _t->featureCountChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->aboutToClose(); break;
        case 7: _t->rebuildStarted(); break;
        case 8: _t->rebuildFinished((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 9: _t->referenceGeometryInitialized(); break;
        case 10: _t->referenceGeometryVisibilityChanged((*reinterpret_cast< ReferenceGeometryType(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 11: _t->treeStructureChanged(); break;
        case 12: _t->allFeaturesLoaded(); break;
        case 13: _t->originReinitializationNeeded(); break;
        case 14: _t->featureShapeUpdated((*reinterpret_cast< Feature*(*)>(_a[1]))); break;
        case 15: _t->trackCenterLinesChanged(); break;
        case 16: _t->onFeatureRebuildRequested(); break;
        case 17: _t->onFeatureChanged(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 2:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< Feature* >(); break;
            }
            break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< Feature* >(); break;
            }
            break;
        case 14:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< Feature* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Document::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::fileNameChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (Document::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::modifiedChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (Document::*)(Feature * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::featureAdded)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (Document::*)(Feature * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::featureAboutToBeRemoved)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (Document::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::featureRemoved)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (Document::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::featureCountChanged)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (Document::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::aboutToClose)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (Document::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::rebuildStarted)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (Document::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::rebuildFinished)) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (Document::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::referenceGeometryInitialized)) {
                *result = 9;
                return;
            }
        }
        {
            using _t = void (Document::*)(ReferenceGeometryType , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::referenceGeometryVisibilityChanged)) {
                *result = 10;
                return;
            }
        }
        {
            using _t = void (Document::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::treeStructureChanged)) {
                *result = 11;
                return;
            }
        }
        {
            using _t = void (Document::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::allFeaturesLoaded)) {
                *result = 12;
                return;
            }
        }
        {
            using _t = void (Document::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::originReinitializationNeeded)) {
                *result = 13;
                return;
            }
        }
        {
            using _t = void (Document::*)(Feature * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::featureShapeUpdated)) {
                *result = 14;
                return;
            }
        }
        {
            using _t = void (Document::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&Document::trackCenterLinesChanged)) {
                *result = 15;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<Document *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< QString*>(_v) = _t->fileName(); break;
        case 1: *reinterpret_cast< bool*>(_v) = _t->isModified(); break;
        case 2: *reinterpret_cast< int*>(_v) = _t->featureCount(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<Document *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setFileName(*reinterpret_cast< QString*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
}

QT_INIT_METAOBJECT const QMetaObject aicad::cad::Document::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__cad__Document.data,
    qt_meta_data_aicad__cad__Document,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::cad::Document::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::cad::Document::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__cad__Document.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::cad::Document::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 3;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 3;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void aicad::cad::Document::fileNameChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::cad::Document::modifiedChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::cad::Document::featureAdded(Feature * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::cad::Document::featureAboutToBeRemoved(Feature * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::cad::Document::featureRemoved()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void aicad::cad::Document::featureCountChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void aicad::cad::Document::aboutToClose()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void aicad::cad::Document::rebuildStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void aicad::cad::Document::rebuildFinished(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void aicad::cad::Document::referenceGeometryInitialized()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void aicad::cad::Document::referenceGeometryVisibilityChanged(ReferenceGeometryType _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void aicad::cad::Document::treeStructureChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void aicad::cad::Document::allFeaturesLoaded()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void aicad::cad::Document::originReinitializationNeeded()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void aicad::cad::Document::featureShapeUpdated(Feature * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 14, _a);
}

// SIGNAL 15
void aicad::cad::Document::trackCenterLinesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 15, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
