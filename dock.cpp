#include <QtDebug>
#include <QJsonDocument>
#include <QJsonArray>

#include "dock.h"
#include "math.h"
#include "../remote-software/sources/entities/entity.h"
#include "../remote-software/sources/entities/remote.h"

void Dock::create(const QVariantMap &config, QObject *entities, QObject *notifications, QObject *api, QObject *configObj)
{
    YioAPIInterface* m_api = qobject_cast<YioAPIInterface *>(api);
    QString mdns = "_yio-dock-api._tcp";


    connect(m_api, &YioAPIInterface::serviceDiscovered, this, [=](QMap<QString, QVariantMap> services){
        QMap<QObject *, QVariant> returnData;
        QVariantList data;

        // let's go through the returned list of discovered docks
        QMap<QString, QVariantMap>::iterator i;
        for (i = services.begin(); i != services.end(); i++)
        {
            DockBase* db = new DockBase(this);
            db->setup(i.value(), entities, notifications, api, configObj);

            QVariantMap d;
            d.insert("id", i.value().value("name").toString());
            d.insert("friendly_name", i.value().value("txt").toMap().value("friendly_name").toString());
            d.insert("mdns", mdns);
            d.insert("type", config.value("type").toString());
            returnData.insert(db, d);
        }

        emit createDone(returnData);
    });

    // start the MDNS discovery
    m_api->discoverNetworkServices(mdns);
}

DockBase::DockBase(QObject* parent)
{
    this->setParent(parent);
}

DockBase::~DockBase() {
    if (m_thread.isRunning()) {
        m_thread.exit();
        m_thread.wait(5000);
    }
}

void DockBase::setup(const QVariantMap& config, QObject* entities, QObject* notifications, QObject* api, QObject* configObj)
{
    setFriendlyName(config.value("txt").toMap().value("friendly_name").toString());
    setIntegrationId(config.value("name").toString());

    // crate a new instance and pass on variables
    DockThread *DThread = new DockThread(config, entities, notifications, api, configObj);
    DThread->m_friendly_name = config.value("txt").toMap().value("friendly_name").toString();

    // move to thread
    DThread->moveToThread(&m_thread);

    // connect signals and slots
    QObject::connect(&m_thread, &QThread::finished, DThread, &QObject::deleteLater);

    QObject::connect(this, &DockBase::connectSignal, DThread, &DockThread::connect);
    QObject::connect(this, &DockBase::disconnectSignal, DThread, &DockThread::disconnect);
    QObject::connect(this, &DockBase::sendCommandSignal, DThread, &DockThread::sendCommand);

    QObject::connect(DThread, &DockThread::stateChanged, this, &DockBase::stateHandler);

    m_thread.start();
}

void DockBase::connect()
{
    emit connectSignal();
}

void DockBase::disconnect()
{
    emit disconnectSignal();
}

void DockBase::sendCommand(const QString& type, const QString& entity_id, const QString& command, const QVariant& param)
{
    emit sendCommandSignal(type, entity_id, command, param);
}

void DockBase::stateHandler(int state)
{
    if (state == 0) {
        setState(CONNECTED);
    } else if (state == 1) {
        setState(CONNECTING);
    } else if (state == 2) {
        setState(DISCONNECTED);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// HOME ASSISTANT THREAD CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DockThread::DockThread(const QVariantMap &config, QObject *entities, QObject *notifications, QObject *api, QObject *configObj)
{
    m_ip = config.value("ip").toString();
    m_token = "0";
    m_id = config.value("name").toString();

    m_entities = qobject_cast<EntitiesInterface *>(entities);
    m_notifications = qobject_cast<NotificationsInterface *>(notifications);
    m_api = qobject_cast<YioAPIInterface *>(api);
    m_config = qobject_cast<ConfigInterface *>(configObj);

    m_websocketReconnect = new QTimer(this);

    m_websocketReconnect->setSingleShot(true);
    m_websocketReconnect->setInterval(2000);
    m_websocketReconnect->stop();

    m_socket = new QWebSocket;
    m_socket->setParent(this);

    QObject::connect(m_socket, SIGNAL(textMessageReceived(const QString &)), this, SLOT(onTextMessageReceived(const QString &)));
    QObject::connect(m_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onError(QAbstractSocket::SocketError)));
    QObject::connect(m_socket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(onStateChanged(QAbstractSocket::SocketState)));

    QObject::connect(m_websocketReconnect, SIGNAL(timeout()), this, SLOT(onTimeout()));
}



void DockThread::onTextMessageReceived(const QString &message)
{
    QJsonParseError parseerror;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseerror);
    if (parseerror.error != QJsonParseError::NoError) {
        qDebug() << "JSON error : " << parseerror.errorString();
        return;
    }
    QVariantMap map = doc.toVariant().toMap();

    QString m = map.value("error").toString();
    if (m.length() > 0) {
        qDebug() << "error : " << m;
    }

    QString type = map.value("type").toString();
    int id = map.value("id").toInt();

    if (type == "auth_required") {
        QString auth = QString("{ \"type\": \"auth\", \"token\": \"%1\" }\n").arg(m_token);
        m_socket->sendTextMessage(auth);
    }

    if (type == "auth_ok") {
        qDebug() << "Connection successful:" << m_friendly_name;
        setState(0);
    }
}

void DockThread::onStateChanged(QAbstractSocket::SocketState state)
{
    if (state == QAbstractSocket::UnconnectedState && !m_userDisconnect) {
        setState(2);
        m_websocketReconnect->start();
    }
}

void DockThread::onError(QAbstractSocket::SocketError error)
{
    qDebug() << error;
    m_socket->close();
    setState(2);
    m_websocketReconnect->start();
}

void DockThread::onTimeout()
{
    if (m_tries == 3) {
        m_websocketReconnect->stop();

        m_notifications->add(true,tr("Cannot connect to ").append(m_friendly_name).append("."), tr("Reconnect"), "dock");
        disconnect();
        m_tries = 0;
    }
    else {
        if (m_state != 1)
        {
            setState(1);
        }
        QString url = QString("ws://").append(m_ip).append(":946");
        m_socket->open(QUrl(url));

        m_tries++;
    }
}

void DockThread::webSocketSendCommand(const QString& domain, const QString& service, const QString& entity_id, QVariantMap *data)
{
//    // sends a command to the YIO dock

//    QVariantMap map;
//    map.insert("type", QVariant("call_service"));
//    map.insert("domain", QVariant(domain));
//    map.insert("service", QVariant(service));

//    if (data == NULL) {
//        QVariantMap d;
//        d.insert("entity_id", QVariant(entity_id));
//        map.insert("service_data", d);
//    }
//    else {
//        data->insert("entity_id", QVariant(entity_id));
//        map.insert("service_data", *data);
//    }
//    QJsonDocument doc = QJsonDocument::fromVariant(map);
//    QString message = doc.toJson(QJsonDocument::JsonFormat::Compact);
//    m_socket->sendTextMessage(message);

}

void DockThread::updateEntity(const QString& entity_id, const QVariantMap& attr)
{
    Entity* entity = (Entity*)m_entities->get(entity_id);
    if (entity) {
    }
}

void DockThread::setState(int state)
{
    m_state = state;
    emit stateChanged(state);
}

void DockThread::connect()
{
    m_userDisconnect = false;

    setState(1);

    // reset the reconnnect trial variable
    m_tries = 0;

    // turn on the websocket connection
    QString url = QString("ws://").append(m_ip).append(":946");
    m_socket->open(QUrl(url));
}

void DockThread::disconnect()
{
    m_userDisconnect = true;

    // turn of the reconnect try
    m_websocketReconnect->stop();

    // turn off the socket
    m_socket->close();

    setState(2);
}

void DockThread::sendCommand(const QString &type, const QString &entity_id, const QString &command, const QVariant &param)
{
    if (type == "remote") {

        // get the remote enityt from the entity database
        Remote* entity = (Remote*)m_entities->get(entity_id);

        // get all the commands the entity can do (IR codes)
        QVariantList commands = entity->commands();

        // find the IR code that matches the command we got from the UI
        QString IRcommand = findIRCode(command, commands);

        // send the request to the dock
        QVariantMap msg;
        msg.insert("type", QVariant("dock"));
        msg.insert("command", QVariant("ir_send"));
        msg.insert("code", IRcommand);
        QJsonDocument doc = QJsonDocument::fromVariant(msg);
        QString message = doc.toJson(QJsonDocument::JsonFormat::Compact);

        if (command != "") {
            // send the message through the websocket api
            m_socket->sendTextMessage(message);
        }

    }
    // commands that does not have entity
    if (type == "dock") {
        if (command == "REMOTE_CHARGED") {
            QVariantMap msg;
            msg.insert("type", QVariant("dock"));
            msg.insert("command", QVariant("remote_charged"));
            QJsonDocument doc = QJsonDocument::fromVariant(msg);
            QString message = doc.toJson(QJsonDocument::JsonFormat::Compact);
            m_socket->sendTextMessage(message);
        } else if (command == "REMOTE_LOWBATTERY") {
            QVariantMap msg;
            msg.insert("type", QVariant("dock"));
            msg.insert("command", QVariant("remote_lowbattery"));
            QJsonDocument doc = QJsonDocument::fromVariant(msg);
            QString message = doc.toJson(QJsonDocument::JsonFormat::Compact);
            m_socket->sendTextMessage(message);
        }
    }
}

QString DockThread::findIRCode(const QString &feature, QVariantList& list)
{
    QString r = "";

    for (int i = 0; i < list.length(); i++) {
        QVariantMap map =  list[i].toMap();

        if (map.value("button_map").toString() == feature) {
            r = map.value("code").toString();
        }
    }

    return r;
}
