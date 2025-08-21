QT       += core gui widgets serialbus serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ModbusSlaveSimulator
TEMPLATE = app
DESTDIR = $$PWD/../

SOURCES += main.cpp\
        mainwindow.cpp

HEADERS  += mainwindow.h \
    src/core/modbusdata.h

# PWD is the directory of the .pro file, so we go up one level to the project root
INCLUDEPATH += $$PWD/../

FORMS    += mainwindow.ui
