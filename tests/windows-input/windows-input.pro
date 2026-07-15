QT += core gui qml testlib
CONFIG += testcase console c++17
TEMPLATE = app
TARGET = windows-input-tests

INCLUDEPATH += ../../app

SOURCES += \
    tst_windowsinput.cpp \
    ../../app/streaming/input/inputgeometry.cpp \
    ../../app/streaming/input/penconversion.cpp \
    ../../app/streaming/input/pointerhistory.cpp \
    ../../app/settings/tabletmappingmanager.cpp

HEADERS += \
    ../../app/streaming/input/inputgeometry.h \
    ../../app/streaming/input/penconversion.h \
    ../../app/streaming/input/pencursorvisibility.h \
    ../../app/streaming/input/pointerhistory.h \
    ../../app/settings/tabletmappingmanager.h
