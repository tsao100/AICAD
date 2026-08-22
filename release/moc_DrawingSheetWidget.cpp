/****************************************************************************
** Meta object code from reading C++ file 'DrawingSheetWidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/drawing/DrawingSheetWidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DrawingSheetWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__drawing__DrawingSheetWidget_t {
    QByteArrayData data[21];
    char stringdata0[289];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__DrawingSheetWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__DrawingSheetWidget_t qt_meta_stringdata_aicad__drawing__DrawingSheetWidget = {
    {
QT_MOC_LITERAL(0, 0, 34), // "aicad::drawing::DrawingSheetW..."
QT_MOC_LITERAL(1, 35, 12), // "viewSelected"
QT_MOC_LITERAL(2, 48, 0), // ""
QT_MOC_LITERAL(3, 49, 12), // "DrawingView*"
QT_MOC_LITERAL(4, 62, 4), // "view"
QT_MOC_LITERAL(5, 67, 17), // "viewDoubleClicked"
QT_MOC_LITERAL(6, 85, 9), // "viewMoved"
QT_MOC_LITERAL(7, 95, 6), // "newPos"
QT_MOC_LITERAL(8, 102, 11), // "zoomChanged"
QT_MOC_LITERAL(9, 114, 4), // "zoom"
QT_MOC_LITERAL(10, 119, 17), // "renderingComplete"
QT_MOC_LITERAL(11, 137, 13), // "triggerRender"
QT_MOC_LITERAL(12, 151, 16), // "triggerRenderAll"
QT_MOC_LITERAL(13, 168, 14), // "onViewRendered"
QT_MOC_LITERAL(14, 183, 6), // "viewId"
QT_MOC_LITERAL(15, 190, 16), // "ViewRenderResult"
QT_MOC_LITERAL(16, 207, 6), // "result"
QT_MOC_LITERAL(17, 214, 20), // "onSheetConfigChanged"
QT_MOC_LITERAL(18, 235, 16), // "onSheetViewAdded"
QT_MOC_LITERAL(19, 252, 18), // "onSheetViewRemoved"
QT_MOC_LITERAL(20, 271, 17) // "onRebuildAllViews"

    },
    "aicad::drawing::DrawingSheetWidget\0"
    "viewSelected\0\0DrawingView*\0view\0"
    "viewDoubleClicked\0viewMoved\0newPos\0"
    "zoomChanged\0zoom\0renderingComplete\0"
    "triggerRender\0triggerRenderAll\0"
    "onViewRendered\0viewId\0ViewRenderResult\0"
    "result\0onSheetConfigChanged\0"
    "onSheetViewAdded\0onSheetViewRemoved\0"
    "onRebuildAllViews"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__DrawingSheetWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      12,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   74,    2, 0x06 /* Public */,
       5,    1,   77,    2, 0x06 /* Public */,
       6,    2,   80,    2, 0x06 /* Public */,
       8,    1,   85,    2, 0x06 /* Public */,
      10,    0,   88,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      11,    1,   89,    2, 0x0a /* Public */,
      12,    0,   92,    2, 0x0a /* Public */,
      13,    2,   93,    2, 0x08 /* Private */,
      17,    0,   98,    2, 0x08 /* Private */,
      18,    1,   99,    2, 0x08 /* Private */,
      19,    1,  102,    2, 0x08 /* Private */,
      20,    0,  105,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3, QMetaType::QPointF,    4,    7,
    QMetaType::Void, QMetaType::Double,    9,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QUuid, 0x80000000 | 15,   14,   16,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::QUuid,   14,
    QMetaType::Void,

       0        // eod
};

void aicad::drawing::DrawingSheetWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DrawingSheetWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->viewSelected((*reinterpret_cast< DrawingView*(*)>(_a[1]))); break;
        case 1: _t->viewDoubleClicked((*reinterpret_cast< DrawingView*(*)>(_a[1]))); break;
        case 2: _t->viewMoved((*reinterpret_cast< DrawingView*(*)>(_a[1])),(*reinterpret_cast< const QPointF(*)>(_a[2]))); break;
        case 3: _t->zoomChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 4: _t->renderingComplete(); break;
        case 5: _t->triggerRender((*reinterpret_cast< DrawingView*(*)>(_a[1]))); break;
        case 6: _t->triggerRenderAll(); break;
        case 7: _t->onViewRendered((*reinterpret_cast< const QUuid(*)>(_a[1])),(*reinterpret_cast< const ViewRenderResult(*)>(_a[2]))); break;
        case 8: _t->onSheetConfigChanged(); break;
        case 9: _t->onSheetViewAdded((*reinterpret_cast< DrawingView*(*)>(_a[1]))); break;
        case 10: _t->onSheetViewRemoved((*reinterpret_cast< const QUuid(*)>(_a[1]))); break;
        case 11: _t->onRebuildAllViews(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DrawingView* >(); break;
            }
            break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DrawingView* >(); break;
            }
            break;
        case 2:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DrawingView* >(); break;
            }
            break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< DrawingView* >(); break;
            }
            break;
        case 9:
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
            using _t = void (DrawingSheetWidget::*)(DrawingView * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheetWidget::viewSelected)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (DrawingSheetWidget::*)(DrawingView * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheetWidget::viewDoubleClicked)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (DrawingSheetWidget::*)(DrawingView * , const QPointF & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheetWidget::viewMoved)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (DrawingSheetWidget::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheetWidget::zoomChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (DrawingSheetWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&DrawingSheetWidget::renderingComplete)) {
                *result = 4;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::DrawingSheetWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__DrawingSheetWidget.data,
    qt_meta_data_aicad__drawing__DrawingSheetWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::DrawingSheetWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::DrawingSheetWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__DrawingSheetWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int aicad::drawing::DrawingSheetWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void aicad::drawing::DrawingSheetWidget::viewSelected(DrawingView * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::drawing::DrawingSheetWidget::viewDoubleClicked(DrawingView * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::drawing::DrawingSheetWidget::viewMoved(DrawingView * _t1, const QPointF & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void aicad::drawing::DrawingSheetWidget::zoomChanged(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::drawing::DrawingSheetWidget::renderingComplete()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
struct qt_meta_stringdata_aicad__drawing__DrawingSheetDialog_t {
    QByteArrayData data[9];
    char stringdata0[121];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__drawing__DrawingSheetDialog_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__drawing__DrawingSheetDialog_t qt_meta_stringdata_aicad__drawing__DrawingSheetDialog = {
    {
QT_MOC_LITERAL(0, 0, 34), // "aicad::drawing::DrawingSheetD..."
QT_MOC_LITERAL(1, 35, 7), // "onApply"
QT_MOC_LITERAL(2, 43, 0), // ""
QT_MOC_LITERAL(3, 44, 4), // "onOk"
QT_MOC_LITERAL(4, 49, 8), // "onCancel"
QT_MOC_LITERAL(5, 58, 11), // "onExportPdf"
QT_MOC_LITERAL(6, 70, 11), // "onExportSvg"
QT_MOC_LITERAL(7, 82, 19), // "onSheetConfigEdited"
QT_MOC_LITERAL(8, 102, 18) // "onTitleBlockEdited"

    },
    "aicad::drawing::DrawingSheetDialog\0"
    "onApply\0\0onOk\0onCancel\0onExportPdf\0"
    "onExportSvg\0onSheetConfigEdited\0"
    "onTitleBlockEdited"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__drawing__DrawingSheetDialog[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   49,    2, 0x08 /* Private */,
       3,    0,   50,    2, 0x08 /* Private */,
       4,    0,   51,    2, 0x08 /* Private */,
       5,    0,   52,    2, 0x08 /* Private */,
       6,    0,   53,    2, 0x08 /* Private */,
       7,    0,   54,    2, 0x08 /* Private */,
       8,    0,   55,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void aicad::drawing::DrawingSheetDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DrawingSheetDialog *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onApply(); break;
        case 1: _t->onOk(); break;
        case 2: _t->onCancel(); break;
        case 3: _t->onExportPdf(); break;
        case 4: _t->onExportSvg(); break;
        case 5: _t->onSheetConfigEdited(); break;
        case 6: _t->onTitleBlockEdited(); break;
        default: ;
        }
    }
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject aicad::drawing::DrawingSheetDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_meta_stringdata_aicad__drawing__DrawingSheetDialog.data,
    qt_meta_data_aicad__drawing__DrawingSheetDialog,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::drawing::DrawingSheetDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::drawing::DrawingSheetDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__drawing__DrawingSheetDialog.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int aicad::drawing::DrawingSheetDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
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
QT_WARNING_POP
QT_END_MOC_NAMESPACE
