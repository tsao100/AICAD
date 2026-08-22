/****************************************************************************
** Meta object code from reading C++ file 'DocumentManager.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/core/DocumentManager.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DocumentManager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__core__DocumentManager_t {
    QByteArrayData data[15];
    char stringdata0[227];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__core__DocumentManager_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__core__DocumentManager_t qt_meta_stringdata_aicad__core__DocumentManager = {
    {
QT_MOC_LITERAL(0, 0, 28), // "aicad::core::DocumentManager"
QT_MOC_LITERAL(1, 29, 15), // "documentCreated"
QT_MOC_LITERAL(2, 45, 0), // ""
QT_MOC_LITERAL(3, 46, 14), // "cad::Document*"
QT_MOC_LITERAL(4, 61, 8), // "document"
QT_MOC_LITERAL(5, 70, 14), // "documentOpened"
QT_MOC_LITERAL(6, 85, 14), // "documentClosed"
QT_MOC_LITERAL(7, 100, 8), // "fileName"
QT_MOC_LITERAL(8, 109, 22), // "currentDocumentChanged"
QT_MOC_LITERAL(9, 132, 20), // "documentCountChanged"
QT_MOC_LITERAL(10, 153, 5), // "count"
QT_MOC_LITERAL(11, 159, 18), // "onDocumentModified"
QT_MOC_LITERAL(12, 178, 8), // "modified"
QT_MOC_LITERAL(13, 187, 25), // "onDocumentFileNameChanged"
QT_MOC_LITERAL(14, 213, 13) // "documentCount"

    },
    "aicad::core::DocumentManager\0"
    "documentCreated\0\0cad::Document*\0"
    "document\0documentOpened\0documentClosed\0"
    "fileName\0currentDocumentChanged\0"
    "documentCountChanged\0count\0"
    "onDocumentModified\0modified\0"
    "onDocumentFileNameChanged\0documentCount"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__core__DocumentManager[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       1,   70, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   49,    2, 0x06 /* Public */,
       5,    1,   52,    2, 0x06 /* Public */,
       6,    1,   55,    2, 0x06 /* Public */,
       8,    1,   58,    2, 0x06 /* Public */,
       9,    1,   61,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      11,    1,   64,    2, 0x08 /* Private */,
      13,    1,   67,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::Int,   10,

 // slots: parameters
    QMetaType::Void, QMetaType::Bool,   12,
    QMetaType::Void, QMetaType::QString,    7,

 // properties: name, type, flags
      14, QMetaType::Int, 0x00495001,

 // properties: notify_signal_id
       4,

       0        // eod
};

void aicad::core::DocumentManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DocumentManager *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->documentCreated((*reinterpret_cast< cad::Document*(*)>(_a[1]))); break;
        case 1: _t->documentOpened((*reinterpret_cast< cad::Document*(*)>(_a[1]))); break;
        case 2: _t->documentClosed((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->currentDocumentChanged((*reinterpret_cast< cad::Document*(*)>(_a[1]))); break;
        case 4: _t->documentCountChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 5: _t->onDocumentModified((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 6: _t->onDocumentFileNameChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (DocumentManager::*)(cad::Document * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DocumentManager::documentCreated)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (DocumentManager::*)(cad::Document * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DocumentManager::documentOpened)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (DocumentManager::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DocumentManager::documentClosed)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (DocumentManager::*)(cad::Document * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DocumentManager::currentDocumentChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (DocumentManager::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DocumentManager::documentCountChanged)) {
                *result = 4;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<DocumentManager *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->documentCount(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
}

QT_INIT_METAOBJECT const QMetaObject aicad::core::DocumentManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__core__DocumentManager.data,
    qt_meta_data_aicad__core__DocumentManager,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::core::DocumentManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::core::DocumentManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__core__DocumentManager.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::core::DocumentManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 1;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void aicad::core::DocumentManager::documentCreated(cad::Document * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::core::DocumentManager::documentOpened(cad::Document * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::core::DocumentManager::documentClosed(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::core::DocumentManager::currentDocumentChanged(cad::Document * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::core::DocumentManager::documentCountChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
