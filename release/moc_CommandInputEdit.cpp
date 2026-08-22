/****************************************************************************
** Meta object code from reading C++ file 'CommandInputEdit.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../src/ui/CommandInputEdit.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CommandInputEdit.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_aicad__ui__CommandInputEdit_t {
    QByteArrayData data[7];
    char stringdata0[93];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_aicad__ui__CommandInputEdit_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_aicad__ui__CommandInputEdit_t qt_meta_stringdata_aicad__ui__CommandInputEdit = {
    {
QT_MOC_LITERAL(0, 0, 27), // "aicad::ui::CommandInputEdit"
QT_MOC_LITERAL(1, 28, 16), // "commandSubmitted"
QT_MOC_LITERAL(2, 45, 0), // ""
QT_MOC_LITERAL(3, 46, 4), // "text"
QT_MOC_LITERAL(4, 51, 17), // "optionChipClicked"
QT_MOC_LITERAL(5, 69, 9), // "optionKey"
QT_MOC_LITERAL(6, 79, 13) // "escapePressed"

    },
    "aicad::ui::CommandInputEdit\0"
    "commandSubmitted\0\0text\0optionChipClicked\0"
    "optionKey\0escapePressed"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_aicad__ui__CommandInputEdit[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   29,    2, 0x06 /* Public */,
       4,    1,   32,    2, 0x06 /* Public */,
       6,    0,   35,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void,

       0        // eod
};

void aicad::ui::CommandInputEdit::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<CommandInputEdit *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->commandSubmitted((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->optionChipClicked((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 2: _t->escapePressed(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (CommandInputEdit::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandInputEdit::commandSubmitted)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (CommandInputEdit::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandInputEdit::optionChipClicked)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (CommandInputEdit::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&CommandInputEdit::escapePressed)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject aicad::ui::CommandInputEdit::staticMetaObject = { {
    QMetaObject::SuperData::link<QLineEdit::staticMetaObject>(),
    qt_meta_stringdata_aicad__ui__CommandInputEdit.data,
    qt_meta_data_aicad__ui__CommandInputEdit,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *aicad::ui::CommandInputEdit::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *aicad::ui::CommandInputEdit::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_aicad__ui__CommandInputEdit.stringdata0))
        return static_cast<void*>(this);
    return QLineEdit::qt_metacast(_clname);
}

int aicad::ui::CommandInputEdit::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QLineEdit::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void aicad::ui::CommandInputEdit::commandSubmitted(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void aicad::ui::CommandInputEdit::optionChipClicked(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void aicad::ui::CommandInputEdit::escapePressed()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
