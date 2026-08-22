/****************************************************************************
** Meta object code from reading C++ file 'CadView.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/view/CadView.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QVector>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CadView.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__view__CadView_t {
    QByteArrayData data[64];
    char stringdata0[905];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__view__CadView_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__view__CadView_t qt_meta_stringdata_aicad__view__CadView = {
    {
QT_MOC_LITERAL(0, 0, 20), // "aicad::view::CadView"
QT_MOC_LITERAL(1, 21, 24), // "returnAlignmentRequested"
QT_MOC_LITERAL(2, 46, 0), // ""
QT_MOC_LITERAL(3, 47, 19), // "featuresRedisplayed"
QT_MOC_LITERAL(4, 67, 10), // "edgePicked"
QT_MOC_LITERAL(5, 78, 15), // "viewTypeChanged"
QT_MOC_LITERAL(6, 94, 8), // "ViewType"
QT_MOC_LITERAL(7, 103, 4), // "type"
QT_MOC_LITERAL(8, 108, 11), // "modeChanged"
QT_MOC_LITERAL(9, 120, 15), // "InteractionMode"
QT_MOC_LITERAL(10, 136, 4), // "mode"
QT_MOC_LITERAL(11, 141, 13), // "pointAcquired"
QT_MOC_LITERAL(12, 155, 5), // "point"
QT_MOC_LITERAL(13, 161, 13), // "geomRefPicked"
QT_MOC_LITERAL(14, 175, 8), // "geomUuid"
QT_MOC_LITERAL(15, 184, 10), // "geomHandle"
QT_MOC_LITERAL(16, 195, 17), // "dimLinePosPreview"
QT_MOC_LITERAL(17, 213, 7), // "offsetX"
QT_MOC_LITERAL(18, 221, 7), // "offsetY"
QT_MOC_LITERAL(19, 229, 19), // "dimLinePosConfirmed"
QT_MOC_LITERAL(20, 249, 18), // "dimLineDragStarted"
QT_MOC_LITERAL(21, 268, 14), // "constraintUuid"
QT_MOC_LITERAL(22, 283, 15), // "dimLineDragging"
QT_MOC_LITERAL(23, 299, 19), // "dimLineDragFinished"
QT_MOC_LITERAL(24, 319, 21), // "dimValueEditCommitted"
QT_MOC_LITERAL(25, 341, 14), // "newExprOrValue"
QT_MOC_LITERAL(26, 356, 14), // "pointCancelled"
QT_MOC_LITERAL(27, 371, 16), // "keyInputReceived"
QT_MOC_LITERAL(28, 388, 3), // "key"
QT_MOC_LITERAL(29, 392, 14), // "objectSelected"
QT_MOC_LITERAL(30, 407, 8), // "objectId"
QT_MOC_LITERAL(31, 416, 11), // "viewClicked"
QT_MOC_LITERAL(32, 428, 9), // "screenPos"
QT_MOC_LITERAL(33, 438, 15), // "Qt::MouseButton"
QT_MOC_LITERAL(34, 454, 6), // "button"
QT_MOC_LITERAL(35, 461, 15), // "viewInitialized"
QT_MOC_LITERAL(36, 477, 13), // "planeSelected"
QT_MOC_LITERAL(37, 491, 9), // "planeName"
QT_MOC_LITERAL(38, 501, 14), // "sketchFinished"
QT_MOC_LITERAL(39, 516, 21), // "sketchRegionsDetected"
QT_MOC_LITERAL(40, 538, 33), // "QVector<aicad::cad::SketchReg..."
QT_MOC_LITERAL(41, 572, 7), // "regions"
QT_MOC_LITERAL(42, 580, 18), // "sketchRegionPicked"
QT_MOC_LITERAL(43, 599, 24), // "aicad::cad::SketchRegion"
QT_MOC_LITERAL(44, 624, 6), // "region"
QT_MOC_LITERAL(45, 631, 22), // "statusMessageRequested"
QT_MOC_LITERAL(46, 654, 3), // "msg"
QT_MOC_LITERAL(47, 658, 9), // "timeoutMs"
QT_MOC_LITERAL(48, 668, 15), // "onSketchRebuilt"
QT_MOC_LITERAL(49, 684, 10), // "setTopView"
QT_MOC_LITERAL(50, 695, 12), // "setFrontView"
QT_MOC_LITERAL(51, 708, 12), // "setRightView"
QT_MOC_LITERAL(52, 721, 16), // "setIsometricView"
QT_MOC_LITERAL(53, 738, 12), // "alignToPlane"
QT_MOC_LITERAL(54, 751, 17), // "const cad::Plane*"
QT_MOC_LITERAL(55, 769, 5), // "plane"
QT_MOC_LITERAL(56, 775, 21), // "onFinishSketchClicked"
QT_MOC_LITERAL(57, 797, 20), // "displaySketchRegions"
QT_MOC_LITERAL(58, 818, 18), // "const cad::Sketch*"
QT_MOC_LITERAL(59, 837, 6), // "sketch"
QT_MOC_LITERAL(60, 844, 18), // "clearSketchRegions"
QT_MOC_LITERAL(61, 863, 21), // "highlightSketchRegion"
QT_MOC_LITERAL(62, 885, 10), // "regionUuid"
QT_MOC_LITERAL(63, 896, 8) // "viewType"

    },
    "aicad::view::CadView\0returnAlignmentRequested\0"
    "\0featuresRedisplayed\0edgePicked\0"
    "viewTypeChanged\0ViewType\0type\0modeChanged\0"
    "InteractionMode\0mode\0pointAcquired\0"
    "point\0geomRefPicked\0geomUuid\0geomHandle\0"
    "dimLinePosPreview\0offsetX\0offsetY\0"
    "dimLinePosConfirmed\0dimLineDragStarted\0"
    "constraintUuid\0dimLineDragging\0"
    "dimLineDragFinished\0dimValueEditCommitted\0"
    "newExprOrValue\0pointCancelled\0"
    "keyInputReceived\0key\0objectSelected\0"
    "objectId\0viewClicked\0screenPos\0"
    "Qt::MouseButton\0button\0viewInitialized\0"
    "planeSelected\0planeName\0sketchFinished\0"
    "sketchRegionsDetected\0"
    "QVector<aicad::cad::SketchRegion>\0"
    "regions\0sketchRegionPicked\0"
    "aicad::cad::SketchRegion\0region\0"
    "statusMessageRequested\0msg\0timeoutMs\0"
    "onSketchRebuilt\0setTopView\0setFrontView\0"
    "setRightView\0setIsometricView\0"
    "alignToPlane\0const cad::Plane*\0plane\0"
    "onFinishSketchClicked\0displaySketchRegions\0"
    "const cad::Sketch*\0sketch\0clearSketchRegions\0"
    "highlightSketchRegion\0regionUuid\0"
    "viewType"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__view__CadView[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      33,   14, // methods
       2,  276, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      23,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,  179,    2, 0x06 /* Public */,
       3,    0,  180,    2, 0x06 /* Public */,
       4,    0,  181,    2, 0x06 /* Public */,
       5,    1,  182,    2, 0x06 /* Public */,
       8,    1,  185,    2, 0x06 /* Public */,
      11,    1,  188,    2, 0x06 /* Public */,
      13,    3,  191,    2, 0x06 /* Public */,
      16,    2,  198,    2, 0x06 /* Public */,
      19,    2,  203,    2, 0x06 /* Public */,
      20,    1,  208,    2, 0x06 /* Public */,
      22,    3,  211,    2, 0x06 /* Public */,
      23,    3,  218,    2, 0x06 /* Public */,
      24,    2,  225,    2, 0x06 /* Public */,
      26,    0,  230,    2, 0x06 /* Public */,
      27,    1,  231,    2, 0x06 /* Public */,
      29,    1,  234,    2, 0x06 /* Public */,
      31,    2,  237,    2, 0x06 /* Public */,
      35,    0,  242,    2, 0x06 /* Public */,
      36,    1,  243,    2, 0x06 /* Public */,
      38,    0,  246,    2, 0x06 /* Public */,
      39,    1,  247,    2, 0x06 /* Public */,
      42,    1,  250,    2, 0x06 /* Public */,
      45,    2,  253,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      48,    0,  258,    2, 0x0a /* Public */,
      49,    0,  259,    2, 0x0a /* Public */,
      50,    0,  260,    2, 0x0a /* Public */,
      51,    0,  261,    2, 0x0a /* Public */,
      52,    0,  262,    2, 0x0a /* Public */,
      53,    1,  263,    2, 0x0a /* Public */,
      56,    0,  266,    2, 0x0a /* Public */,
      57,    2,  267,    2, 0x0a /* Public */,
      60,    0,  272,    2, 0x0a /* Public */,
      61,    1,  273,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 6,    7,
    QMetaType::Void, 0x80000000 | 9,   10,
    QMetaType::Void, QMetaType::QPointF,   12,
    QMetaType::Void, QMetaType::QPointF, QMetaType::QString, QMetaType::Int,   12,   14,   15,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,   17,   18,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,   17,   18,
    QMetaType::Void, QMetaType::QString,   21,
    QMetaType::Void, QMetaType::QString, QMetaType::Double, QMetaType::Double,   21,   17,   18,
    QMetaType::Void, QMetaType::QString, QMetaType::Double, QMetaType::Double,   21,   17,   18,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   21,   25,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   28,
    QMetaType::Void, QMetaType::Int,   30,
    QMetaType::Void, QMetaType::QPoint, 0x80000000 | 33,   32,   34,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   37,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 40,   41,
    QMetaType::Void, 0x80000000 | 43,   44,
    QMetaType::Void, QMetaType::QString, QMetaType::Int,   46,   47,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 54,   55,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 40, 0x80000000 | 58,   41,   59,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   62,

 // properties: name, type, flags
      63, 0x80000000 | 6, 0x0049510b,
      10, 0x80000000 | 9, 0x0049510b,

 // properties: notify_signal_id
       3,
       4,

       0        // eod
};

void aicad::view::CadView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<CadView *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->returnAlignmentRequested(); break;
        case 1: _t->featuresRedisplayed(); break;
        case 2: _t->edgePicked(); break;
        case 3: _t->viewTypeChanged((*reinterpret_cast< ViewType(*)>(_a[1]))); break;
        case 4: _t->modeChanged((*reinterpret_cast< InteractionMode(*)>(_a[1]))); break;
        case 5: _t->pointAcquired((*reinterpret_cast< QPointF(*)>(_a[1]))); break;
        case 6: _t->geomRefPicked((*reinterpret_cast< QPointF(*)>(_a[1])),(*reinterpret_cast< QString(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 7: _t->dimLinePosPreview((*reinterpret_cast< double(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2]))); break;
        case 8: _t->dimLinePosConfirmed((*reinterpret_cast< double(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2]))); break;
        case 9: _t->dimLineDragStarted((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 10: _t->dimLineDragging((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3]))); break;
        case 11: _t->dimLineDragFinished((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3]))); break;
        case 12: _t->dimValueEditCommitted((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 13: _t->pointCancelled(); break;
        case 14: _t->keyInputReceived((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 15: _t->objectSelected((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 16: _t->viewClicked((*reinterpret_cast< QPoint(*)>(_a[1])),(*reinterpret_cast< Qt::MouseButton(*)>(_a[2]))); break;
        case 17: _t->viewInitialized(); break;
        case 18: _t->planeSelected((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 19: _t->sketchFinished(); break;
        case 20: _t->sketchRegionsDetected((*reinterpret_cast< const QVector<aicad::cad::SketchRegion>(*)>(_a[1]))); break;
        case 21: _t->sketchRegionPicked((*reinterpret_cast< const aicad::cad::SketchRegion(*)>(_a[1]))); break;
        case 22: _t->statusMessageRequested((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 23: _t->onSketchRebuilt(); break;
        case 24: _t->setTopView(); break;
        case 25: _t->setFrontView(); break;
        case 26: _t->setRightView(); break;
        case 27: _t->setIsometricView(); break;
        case 28: _t->alignToPlane((*reinterpret_cast< const cad::Plane*(*)>(_a[1]))); break;
        case 29: _t->onFinishSketchClicked(); break;
        case 30: _t->displaySketchRegions((*reinterpret_cast< const QVector<aicad::cad::SketchRegion>(*)>(_a[1])),(*reinterpret_cast< const cad::Sketch*(*)>(_a[2]))); break;
        case 31: _t->clearSketchRegions(); break;
        case 32: _t->highlightSketchRegion((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (CadView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::returnAlignmentRequested)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (CadView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::featuresRedisplayed)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (CadView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::edgePicked)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (CadView::*)(ViewType );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::viewTypeChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (CadView::*)(InteractionMode );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::modeChanged)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (CadView::*)(QPointF );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::pointAcquired)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (CadView::*)(QPointF , QString , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::geomRefPicked)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (CadView::*)(double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::dimLinePosPreview)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (CadView::*)(double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::dimLinePosConfirmed)) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (CadView::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::dimLineDragStarted)) {
                *result = 9;
                return;
            }
        }
        {
            using _t = void (CadView::*)(const QString & , double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::dimLineDragging)) {
                *result = 10;
                return;
            }
        }
        {
            using _t = void (CadView::*)(const QString & , double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::dimLineDragFinished)) {
                *result = 11;
                return;
            }
        }
        {
            using _t = void (CadView::*)(const QString & , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::dimValueEditCommitted)) {
                *result = 12;
                return;
            }
        }
        {
            using _t = void (CadView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::pointCancelled)) {
                *result = 13;
                return;
            }
        }
        {
            using _t = void (CadView::*)(QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::keyInputReceived)) {
                *result = 14;
                return;
            }
        }
        {
            using _t = void (CadView::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::objectSelected)) {
                *result = 15;
                return;
            }
        }
        {
            using _t = void (CadView::*)(QPoint , Qt::MouseButton );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::viewClicked)) {
                *result = 16;
                return;
            }
        }
        {
            using _t = void (CadView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::viewInitialized)) {
                *result = 17;
                return;
            }
        }
        {
            using _t = void (CadView::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::planeSelected)) {
                *result = 18;
                return;
            }
        }
        {
            using _t = void (CadView::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::sketchFinished)) {
                *result = 19;
                return;
            }
        }
        {
            using _t = void (CadView::*)(const QVector<aicad::cad::SketchRegion> & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::sketchRegionsDetected)) {
                *result = 20;
                return;
            }
        }
        {
            using _t = void (CadView::*)(const aicad::cad::SketchRegion & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::sketchRegionPicked)) {
                *result = 21;
                return;
            }
        }
        {
            using _t = void (CadView::*)(const QString & , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CadView::statusMessageRequested)) {
                *result = 22;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<CadView *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< ViewType*>(_v) = _t->viewType(); break;
        case 1: *reinterpret_cast< InteractionMode*>(_v) = _t->mode(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<CadView *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setViewType(*reinterpret_cast< ViewType*>(_v)); break;
        case 1: _t->setMode(*reinterpret_cast< InteractionMode*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    }
#endif // QT_NO_PROPERTIES
}

QT_INIT_METAOBJECT const QMetaObject aicad::view::CadView::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_aicad__view__CadView.data,
    qt_meta_data_aicad__view__CadView,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::view::CadView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::view::CadView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__view__CadView.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int aicad::view::CadView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 33)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 33;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 33)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 33;
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 2;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 2;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void aicad::view::CadView::returnAlignmentRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void aicad::view::CadView::featuresRedisplayed()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void aicad::view::CadView::edgePicked()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void aicad::view::CadView::viewTypeChanged(ViewType _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void aicad::view::CadView::modeChanged(InteractionMode _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void aicad::view::CadView::pointAcquired(QPointF _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void aicad::view::CadView::geomRefPicked(QPointF _t1, QString _t2, int _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void aicad::view::CadView::dimLinePosPreview(double _t1, double _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void aicad::view::CadView::dimLinePosConfirmed(double _t1, double _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void aicad::view::CadView::dimLineDragStarted(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void aicad::view::CadView::dimLineDragging(const QString & _t1, double _t2, double _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void aicad::view::CadView::dimLineDragFinished(const QString & _t1, double _t2, double _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void aicad::view::CadView::dimValueEditCommitted(const QString & _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}

// SIGNAL 13
void aicad::view::CadView::pointCancelled()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void aicad::view::CadView::keyInputReceived(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 14, _a);
}

// SIGNAL 15
void aicad::view::CadView::objectSelected(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 15, _a);
}

// SIGNAL 16
void aicad::view::CadView::viewClicked(QPoint _t1, Qt::MouseButton _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 16, _a);
}

// SIGNAL 17
void aicad::view::CadView::viewInitialized()
{
    QMetaObject::activate(this, &staticMetaObject, 17, nullptr);
}

// SIGNAL 18
void aicad::view::CadView::planeSelected(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 18, _a);
}

// SIGNAL 19
void aicad::view::CadView::sketchFinished()
{
    QMetaObject::activate(this, &staticMetaObject, 19, nullptr);
}

// SIGNAL 20
void aicad::view::CadView::sketchRegionsDetected(const QVector<aicad::cad::SketchRegion> & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 20, _a);
}

// SIGNAL 21
void aicad::view::CadView::sketchRegionPicked(const aicad::cad::SketchRegion & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 21, _a);
}

// SIGNAL 22
void aicad::view::CadView::statusMessageRequested(const QString & _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 22, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
