TARGET = qapp-process-lock-sample
HEADERS = *.hpp
SOURCES = *.cpp

QT += widgets

QMAKE_CXXFLAGS += -std=c++11

# qdebug
DEFINES += QAPP_PROCESS_LOCK_LOG_DEBUG
CONFIG += console

