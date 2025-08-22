QT       += core gui network serialport serialbus

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

INCLUDEPATH += $$PWD/core \
               $$PWD/devices \
               $$PWD/protocols
DESTDIR = $$PWD/../

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    core/Device.cpp \
    core/DeviceManager.cpp \
    core/ProtocolHandler.cpp \
    core/ThreadManager.cpp \
    core/DataManager.cpp \
    devices/LSJDevice.cpp \
    devices/JGQDevice.cpp

HEADERS += \
    core/modbusdata.h \
    mainwindow.h \
    core/Device.h \
    core/DeviceManager.h \
    core/ProtocolHandler.h \
    core/ThreadManager.h \
    core/DataManager.h \
    devices/LSJDevice.h \
    devices/JGQDevice.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# 强制MSVC编译器使用UTF-8编码来解析源文件和执行字符集
win32-msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
