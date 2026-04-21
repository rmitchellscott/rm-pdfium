TEMPLATE = lib
TARGET = rm-pdfium
CONFIG += shared plugin no_plugin_name_prefix
QT =

xoviextension.target = xovi.c
xoviextension.commands = python3 $$(XOVI_REPO)/util/xovigen.py -o xovi.c -H xovi.h rm-pdfium.xovi
xoviextension.depends = rm-pdfium.xovi

QMAKE_EXTRA_TARGETS += xoviextension
PRE_TARGETDEPS += xovi.c

SOURCES += src/main.c xovi.c

LIBS += -ldl

QMAKE_CFLAGS += -fPIC
