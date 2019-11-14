TEMPLATE        = lib
CONFIG         += plugin
QT             += websockets core quick

REMOTE_SRC = $$(YIO_SRC)
isEmpty(REMOTE_SRC) {
    REMOTE_SRC = $$clean_path($$PWD/../remote-software)
    warning("Environment variables YIO_SR not defined! Using '$$REMOTE_SRC' for remote-software project.")
} else {
    REMOTE_SRC = $$(YIO_SRC)/remote-software
    message("YIO_SRC is set: using '$$REMOTE_SRC' for remote-software project.")
}

! include($$REMOTE_SRC/qmake-target-platform.pri) {
    error( "Couldn't find the qmake-target-platform.pri file!" )
}
! include($$REMOTE_SRC/qmake-destination-path.pri) {
    error( "Couldn't find the qmake-destination-path.pri file!" )
}

HEADERS         = dock.h \
                  $$REMOTE_SRC/sources/integrations/integration.h \
                  $$REMOTE_SRC/sources/integrations/integrationinterface.h \
                  $$REMOTE_SRC/sources/yioapiinterface.h
SOURCES         = dock.cpp
TARGET          = dock

# Configure destination path. DESTDIR is set in qmake-destination-path.pri
OBJECTS_DIR = $$PWD/build/$$DESTINATION_PATH/obj
MOC_DIR = $$PWD/build/$$DESTINATION_PATH/moc
RCC_DIR = $$PWD/build/$$DESTINATION_PATH/qrc
UI_DIR = $$PWD/build/$$DESTINATION_PATH/ui

# install
unix {
    target.path = /usr/lib
    INSTALLS += target
}
