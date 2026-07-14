QT += core testlib
CONFIG += testcase console c++17
TEMPLATE = app
TARGET = windows-input-tests

INCLUDEPATH += ../../app

SOURCES += \
    tst_windowsinput.cpp \
    ../../app/streaming/input/inputgeometry.cpp \
    ../../app/streaming/input/penconversion.cpp

HEADERS += \
    ../../app/streaming/input/inputgeometry.h \
    ../../app/streaming/input/penconversion.h
