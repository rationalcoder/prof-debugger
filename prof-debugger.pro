TEMPLATE = app
TARGET = pdbg
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp

HEADERS += \
    bucket_array.hpp

isEmpty(FMT_PATH) {
    FMT_PATH = $$(FMT_PATH)
    isEmpty(FMT_PATH) {
        FMT_PATH = ../fmt
    }
}

INCLUDEPATH += $$FMT_PATH/include
