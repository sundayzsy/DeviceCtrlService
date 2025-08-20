QT       += core gui widgets serialbus serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ModbusSlaveSimulator
TEMPLATE = app

SOURCES += main.cpp\
        mainwindow.cpp

HEADERS  += mainwindow.h

FORMS    += mainwindow.ui