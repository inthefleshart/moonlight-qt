TEMPLATE = app
TARGET = MoonlightPenDiagnostics
CONFIG += c++17 windows
QT -= core gui
SOURCES += main.cpp
LIBS += user32.lib gdi32.lib
