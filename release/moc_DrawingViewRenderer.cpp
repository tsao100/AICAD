/****************************************************************************
** Meta object code from reading C++ file 'DrawingViewRenderer.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/drawing/DrawingViewRenderer.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DrawingViewRenderer.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__drawing__DrawingViewRenderer_t {
    QByteArrayData data[11];
    char stringdata0[141];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__DrawingViewRenderer_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__DrawingViewRenderer_t qt_meta_stringdata_aicad__drawing__DrawingViewRenderer = {
    {
QT_MOC_LITERAL(0, 0, 35), // "aicad::drawing::DrawingViewRe..."
QT_MOC_LITERAL(1, 36, 8), // "rendered"
QT_MOC_LITERAL(2, 45, 0), // ""
QT_MOC_LITERAL(3, 46, 6), // "viewId"
QT_MOC_LITERAL(4, 53, 16), // "ViewRenderResult"
QT_MOC_LITERAL(5, 70, 6), // "result"
QT_MOC_LITERAL(6, 77, 11), // "renderError"
QT_MOC_LITERAL(7, 89, 7), // "message"
QT_MOC_LITERAL(8, 97, 14), // "renderProgress"
QT_MOC_LITERAL(9, 112, 7), // "percent"
QT_MOC_LITERAL(10, 120, 20) // "onRenderTaskFinished"

    },
    "aicad::drawing::DrawingViewRenderer\0"
    "rendered\0\0viewId\0ViewRenderResult\0"
    "result\0renderError\0message\0renderProgress\0"
    "percent\0onRenderTaskFinished"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__DrawingViewRenderer[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    2,   34,    2, 0x06 /* Public */,
       6,    2,   39,    2, 0x06 /* Public */,
       8,    2,   44,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      10,    2,   49,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QUuid, 0x80000000 | 4,    3,    5,
    QMetaType::Void, QMetaType::QUuid, QMetaType::QString,    3,    7,
    QMetaType::Void, QMetaType::QUuid, QMetaType::Int,    3,    9,

 // slots: parameters
    QMetaType::Void, QMetaType::QUuid, 0x80000000 | 4,    3,    5,

       0        // eod
};

void aicad::drawing::DrawingViewRenderer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DrawingViewRenderer *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->rendered((*reinterpret_cast< const QUuid(*)>(_a[1])),(*reinterpret_cast< const ViewRenderResult(*)>(_a[2]))); break;
        case 1: _t->renderError((*reinterpret_cast< const QUuid(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 2: _t->renderProgress((*reinterpret_cast< const QUuid(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 3: _t->onRenderTaskFinished((*reinterpret_cast< const QUuid(*)>(_a[1])),(*reinterpret_cast< const ViewRenderResult(*)>(_a[2]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (DrawingViewRenderer::*)(const QUuid & , const ViewRenderResult & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingViewRenderer::rendered)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (DrawingViewRenderer::*)(const QUuid & , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingViewRenderer::renderError)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (DrawingViewRenderer::*)(const QUuid & , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingViewRenderer::renderProgress)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::DrawingViewRenderer::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__DrawingViewRenderer.data,
    qt_meta_data_aicad__drawing__DrawingViewRenderer,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::DrawingViewRenderer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::DrawingViewRenderer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__DrawingViewRenderer.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int aicad::drawing::DrawingViewRenderer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
void aicad::drawing::DrawingViewRenderer::rendered(const QUuid & _t1, const ViewRenderResult & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::drawing::DrawingViewRenderer::renderError(const QUuid & _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::drawing::DrawingViewRenderer::renderProgress(const QUuid & _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
