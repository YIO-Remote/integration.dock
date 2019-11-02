TEMPLATE        = lib
CONFIG         += plugin
QT             += websockets core quick
HEADERS         = dock.h \
                  ../remote-software/sources/integrations/integration.h \
                  ../remote-software/sources/integrations/integrationinterface.h
SOURCES         = dock.cpp
TARGET          = dock
DESTDIR         = ../remote-software/plugins

#DISTFILES += dock.json

# install
unix {
    target.path = /usr/lib
    INSTALLS += target
}
