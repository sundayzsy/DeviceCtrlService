QT       += core gui widgets serialbus serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ModbusSlaveSimulator
TEMPLATE = app
DESTDIR = $$PWD/../SimulatorExe

SOURCES += main.cpp\
        mainwindow.cpp

HEADERS  += mainwindow.h \
    src/core/modbusdata.h

# PWD is the directory of the .pro file, so we go up one level to the project root
INCLUDEPATH += $$PWD/../

FORMS    += mainwindow.ui

# 强制MSVC编译器使用UTF-8编码来解析源文件和执行字符集
win32-msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
