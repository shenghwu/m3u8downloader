QT       += core gui network concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    m3u8downloader.cpp \
    main.cpp \
    mainwindow.cpp \
    # tsdownloader.cpp \
    # utils.cpp

HEADERS += \
    m3u8downloader.h \
    mainwindow.h \
    # tsdownloader.h \
    # utils.h

#
INCLUDEPATH += "C:/msys64/mingw64/include"
LIBS += -L"C:/msys64/mingw64/lib" -lcrypto -lssl

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
