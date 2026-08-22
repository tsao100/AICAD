/****************************************************************************
** Meta object code from reading C++ file 'SketchPanel.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/ui/SketchPanel.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'SketchPanel.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__ui__SketchPanel_t {
    QByteArrayData data[47];
    char stringdata0[806];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__ui__SketchPanel_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__ui__SketchPanel_t qt_meta_stringdata_aicad__ui__SketchPanel = {
    {
QT_MOC_LITERAL(0, 0, 22), // "aicad::ui::SketchPanel"
QT_MOC_LITERAL(1, 23, 26), // "requestAddConstructionLine"
QT_MOC_LITERAL(2, 50, 0), // ""
QT_MOC_LITERAL(3, 51, 20), // "requestAddCenterline"
QT_MOC_LITERAL(4, 72, 28), // "requestAddConstructionCircle"
QT_MOC_LITERAL(5, 101, 25), // "requestToggleConstruction"
QT_MOC_LITERAL(6, 127, 17), // "requestConstraint"
QT_MOC_LITERAL(7, 145, 19), // "cad::ConstraintType"
QT_MOC_LITERAL(8, 165, 4), // "type"
QT_MOC_LITERAL(9, 170, 26), // "requestConstraintWithValue"
QT_MOC_LITERAL(10, 197, 5), // "value"
QT_MOC_LITERAL(11, 203, 33), // "requestConstraintWithValueAnd..."
QT_MOC_LITERAL(12, 237, 9), // "paramExpr"
QT_MOC_LITERAL(13, 247, 7), // "driving"
QT_MOC_LITERAL(14, 255, 23), // "requestRemoveConstraint"
QT_MOC_LITERAL(15, 279, 4), // "uuid"
QT_MOC_LITERAL(16, 284, 21), // "requestEditConstraint"
QT_MOC_LITERAL(17, 306, 7), // "newExpr"
QT_MOC_LITERAL(18, 314, 8), // "newValue"
QT_MOC_LITERAL(19, 323, 15), // "isLiteralNumber"
QT_MOC_LITERAL(20, 339, 12), // "requestSolve"
QT_MOC_LITERAL(21, 352, 24), // "regionDetectionRequested"
QT_MOC_LITERAL(22, 377, 14), // "regionSelected"
QT_MOC_LITERAL(23, 392, 10), // "regionUuid"
QT_MOC_LITERAL(24, 403, 26), // "dimensionConstraintClicked"
QT_MOC_LITERAL(25, 430, 14), // "constraintUuid"
QT_MOC_LITERAL(26, 445, 35), // "cad::ConstraintOverlayManager..."
QT_MOC_LITERAL(27, 481, 4), // "mode"
QT_MOC_LITERAL(28, 486, 10), // "instanceId"
QT_MOC_LITERAL(29, 497, 17), // "onConstraintAdded"
QT_MOC_LITERAL(30, 515, 19), // "onConstraintRemoved"
QT_MOC_LITERAL(31, 535, 18), // "onConstraintSolved"
QT_MOC_LITERAL(32, 554, 16), // "cad::SolveResult"
QT_MOC_LITERAL(33, 571, 6), // "result"
QT_MOC_LITERAL(34, 578, 17), // "onGeometryChanged"
QT_MOC_LITERAL(35, 596, 23), // "onConstraintItemClicked"
QT_MOC_LITERAL(36, 620, 16), // "QTreeWidgetItem*"
QT_MOC_LITERAL(37, 637, 4), // "item"
QT_MOC_LITERAL(38, 642, 3), // "col"
QT_MOC_LITERAL(39, 646, 25), // "onRemoveConstraintClicked"
QT_MOC_LITERAL(40, 672, 14), // "onSolveClicked"
QT_MOC_LITERAL(41, 687, 18), // "onDimensionClicked"
QT_MOC_LITERAL(42, 706, 29), // "onConstraintItemDoubleClicked"
QT_MOC_LITERAL(43, 736, 28), // "onConstraintReadyFromSession"
QT_MOC_LITERAL(44, 765, 19), // "QList<cad::GeomRef>"
QT_MOC_LITERAL(45, 785, 4), // "refs"
QT_MOC_LITERAL(46, 790, 15) // "onToggleOverlay"

    },
    "aicad::ui::SketchPanel\0"
    "requestAddConstructionLine\0\0"
    "requestAddCenterline\0requestAddConstructionCircle\0"
    "requestToggleConstruction\0requestConstraint\0"
    "cad::ConstraintType\0type\0"
    "requestConstraintWithValue\0value\0"
    "requestConstraintWithValueAndExpr\0"
    "paramExpr\0driving\0requestRemoveConstraint\0"
    "uuid\0requestEditConstraint\0newExpr\0"
    "newValue\0isLiteralNumber\0requestSolve\0"
    "regionDetectionRequested\0regionSelected\0"
    "regionUuid\0dimensionConstraintClicked\0"
    "constraintUuid\0cad::ConstraintOverlayManager::Mode\0"
    "mode\0instanceId\0onConstraintAdded\0"
    "onConstraintRemoved\0onConstraintSolved\0"
    "cad::SolveResult\0result\0onGeometryChanged\0"
    "onConstraintItemClicked\0QTreeWidgetItem*\0"
    "item\0col\0onRemoveConstraintClicked\0"
    "onSolveClicked\0onDimensionClicked\0"
    "onConstraintItemDoubleClicked\0"
    "onConstraintReadyFromSession\0"
    "QList<cad::GeomRef>\0refs\0onToggleOverlay"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__ui__SketchPanel[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      24,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      13,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,  134,    2, 0x06 /* Public */,
       3,    0,  135,    2, 0x06 /* Public */,
       4,    0,  136,    2, 0x06 /* Public */,
       5,    0,  137,    2, 0x06 /* Public */,
       6,    1,  138,    2, 0x06 /* Public */,
       9,    2,  141,    2, 0x06 /* Public */,
      11,    4,  146,    2, 0x06 /* Public */,
      14,    1,  155,    2, 0x06 /* Public */,
      16,    4,  158,    2, 0x06 /* Public */,
      20,    0,  167,    2, 0x06 /* Public */,
      21,    0,  168,    2, 0x06 /* Public */,
      22,    1,  169,    2, 0x06 /* Public */,
      24,    3,  172,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      29,    1,  179,    2, 0x08 /* Private */,
      30,    1,  182,    2, 0x08 /* Private */,
      31,    1,  185,    2, 0x08 /* Private */,
      34,    0,  188,    2, 0x08 /* Private */,
      35,    2,  189,    2, 0x08 /* Private */,
      39,    0,  194,    2, 0x08 /* Private */,
      40,    0,  195,    2, 0x08 /* Private */,
      41,    3,  196,    2, 0x08 /* Private */,
      42,    2,  203,    2, 0x08 /* Private */,
      43,    5,  208,    2, 0x08 /* Private */,
      46,    0,  219,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void, 0x80000000 | 7, QMetaType::Double,    8,   10,
    QMetaType::Void, 0x80000000 | 7, QMetaType::Double, QMetaType::QString, QMetaType::Bool,    8,   10,   12,   13,
    QMetaType::Void, QMetaType::QString,   15,
    QMetaType::Void, QMetaType::QString, QMetaType::QString, QMetaType::Double, QMetaType::Bool,   15,   17,   18,   19,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   23,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 26, QMetaType::QString,   25,   27,   28,

 // slots: parameters
    QMetaType::Void, QMetaType::QString,   15,
    QMetaType::Void, QMetaType::QString,   15,
    QMetaType::Void, 0x80000000 | 32,   33,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 36, QMetaType::Int,   37,   38,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 26, QMetaType::QString,   15,   27,   28,
    QMetaType::Void, 0x80000000 | 36, QMetaType::Int,   37,   38,
    QMetaType::Void, 0x80000000 | 44, QMetaType::Double, QMetaType::QString, QMetaType::Bool, 0x80000000 | 7,   45,   10,   12,   13,    8,
    QMetaType::Void,

       0        // eod
};

void aicad::ui::SketchPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SketchPanel *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->requestAddConstructionLine(); break;
        case 1: _t->requestAddCenterline(); break;
        case 2: _t->requestAddConstructionCircle(); break;
        case 3: _t->requestToggleConstruction(); break;
        case 4: _t->requestConstraint((*reinterpret_cast< cad::ConstraintType(*)>(_a[1]))); break;
        case 5: _t->requestConstraintWithValue((*reinterpret_cast< cad::ConstraintType(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2]))); break;
        case 6: _t->requestConstraintWithValueAndExpr((*reinterpret_cast< cad::ConstraintType(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3])),(*reinterpret_cast< bool(*)>(_a[4]))); break;
        case 7: _t->requestRemoveConstraint((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 8: _t->requestEditConstraint((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3])),(*reinterpret_cast< bool(*)>(_a[4]))); break;
        case 9: _t->requestSolve(); break;
        case 10: _t->regionDetectionRequested(); break;
        case 11: _t->regionSelected((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 12: _t->dimensionConstraintClicked((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< cad::ConstraintOverlayManager::Mode(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 13: _t->onConstraintAdded((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 14: _t->onConstraintRemoved((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 15: _t->onConstraintSolved((*reinterpret_cast< cad::SolveResult(*)>(_a[1]))); break;
        case 16: _t->onGeometryChanged(); break;
        case 17: _t->onConstraintItemClicked((*reinterpret_cast< QTreeWidgetItem*(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 18: _t->onRemoveConstraintClicked(); break;
        case 19: _t->onSolveClicked(); break;
        case 20: _t->onDimensionClicked((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< cad::ConstraintOverlayManager::Mode(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 21: _t->onConstraintItemDoubleClicked((*reinterpret_cast< QTreeWidgetItem*(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 22: _t->onConstraintReadyFromSession((*reinterpret_cast< const QList<cad::GeomRef>(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3])),(*reinterpret_cast< bool(*)>(_a[4])),(*reinterpret_cast< cad::ConstraintType(*)>(_a[5]))); break;
        case 23: _t->onToggleOverlay(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (SketchPanel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestAddConstructionLine)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestAddCenterline)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestAddConstructionCircle)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestToggleConstruction)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)(cad::ConstraintType );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestConstraint)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)(cad::ConstraintType , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestConstraintWithValue)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)(cad::ConstraintType , double , const QString & , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestConstraintWithValueAndExpr)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestRemoveConstraint)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)(const QString & , const QString & , double , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestEditConstraint)) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::requestSolve)) {
                *result = 9;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::regionDetectionRequested)) {
                *result = 10;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::regionSelected)) {
                *result = 11;
                return;
            }
        }
        {
            using _t = void (SketchPanel::*)(const QString & , cad::ConstraintOverlayManager::Mode , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SketchPanel::dimensionConstraintClicked)) {
                *result = 12;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::ui::SketchPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<QDockWidget::staticMetaObject>(),
    qt_meta_stringdata_aicad__ui__SketchPanel.data,
    qt_meta_data_aicad__ui__SketchPanel,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::ui::SketchPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::ui::SketchPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__ui__SketchPanel.stringdata0))
        return static_cast<void*>(this);
    return QDockWidget::qt_metacast(_clname);
}

int aicad::ui::SketchPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDockWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 24)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 24;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 24)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 24;
    }
    return _id;
}

// SIGNAL 0
void aicad::ui::SketchPanel::requestAddConstructionLine()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void aicad::ui::SketchPanel::requestAddCenterline()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void aicad::ui::SketchPanel::requestAddConstructionCircle()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void aicad::ui::SketchPanel::requestToggleConstruction()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void aicad::ui::SketchPanel::requestConstraint(cad::ConstraintType _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void aicad::ui::SketchPanel::requestConstraintWithValue(cad::ConstraintType _t1, double _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void aicad::ui::SketchPanel::requestConstraintWithValueAndExpr(cad::ConstraintType _t1, double _t2, const QString & _t3, bool _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void aicad::ui::SketchPanel::requestRemoveConstraint(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void aicad::ui::SketchPanel::requestEditConstraint(const QString & _t1, const QString & _t2, double _t3, bool _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void aicad::ui::SketchPanel::requestSolve()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void aicad::ui::SketchPanel::regionDetectionRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void aicad::ui::SketchPanel::regionSelected(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void aicad::ui::SketchPanel::dimensionConstraintClicked(const QString & _t1, cad::ConstraintOverlayManager::Mode _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
