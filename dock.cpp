/******************************************************************************
  *
  * Copyright (C) 2019 Marton Borzak <hello@martonborzak.com>
  * Copyright (C) 2019 Christian Riedl <ric@rts.co.at>
  *
  * This file is part of the YIO-Remote software project.
  *
  * YIO-Remote software is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * YIO-Remote software is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with YIO-Remote software. If not, see <https://www.gnu.org/licenses/>.
  *
  * SPDX-License-Identifier: GPL-3.0-or-later
 *****************************************************************************/

#include <QtDebug>
#include <QJsonDocument>
#include <QJsonArray>

#include "dock.h"
#include "math.h"
#include "../remote-software/sources/entities/entity.h"
#include "../remote-software/sources/entities/remoteinterface.h"

IntegrationInterface::~IntegrationInterface()
{}

void DockPlugin::create(const QVariantMap &config, QObject *entities, QObject *notifications, QObject *api, QObject *configObj)
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
            DockBase* db = new DockBase(m_log, this);
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

    // start a timeout timer if no docks are discovered
    QTimer* timeOutTimer = new QTimer();
    timeOutTimer->setSingleShot(true);
    connect(timeOutTimer, &QTimer::timeout, this, [=](){
        QMap<QObject *, QVariant> returnData;
        emit createDone(returnData);
    });
    timeOutTimer->start(5000);
}

DockBase::DockBase(QLoggingCategory& log, QObject* parent) :
    m_log(log)
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
    Integration::setup(config, entities);

    // crate a new instance and pass on variables
    DockThread *DThread = new DockThread(config, entities, notifications, api, configObj, m_log);
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

void DockBase::sendCommand(const QString& type, const QString& entity_id, int command, const QVariant& param)
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

DockThread::DockThread(const QVariantMap &config, QObject *entities, QObject *notifications, QObject *api, QObject *configObj,
                       QLoggingCategory& log) :
    m_log(log)
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
        qCDebug(m_log) << "JSON error : " << parseerror.errorString();
        return;
    }
    QVariantMap map = doc.toVariant().toMap();

    QString m = map.value("error").toString();
    if (m.length() > 0) {
        qCDebug(m_log) << "error : " << m;
    }

    QString type = map.value("type").toString();
    //int id = map.value("id").toInt();

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
    qCDebug(m_log) << error;
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

void DockThread::sendCommand(const QString &type, const QString &entity_id, int command, const QVariant &param)
{
    Q_UNUSED(param)
    if (type == "remote") {

        // get the remote enityt from the entity database
        EntityInterface* entity = m_entities->getEntityInterface(entity_id);
        RemoteInterface* remoteInterface = static_cast<RemoteInterface*>(entity->getSpecificInterface());

        // get all the commands the entity can do (IR codes)
        QVariantList commands = remoteInterface->commands();

        // find the IR code that matches the command we got from the UI
        QString commandText = entity->getCommandName(command);
        QString IRcommand = findIRCode(commandText, commands);

        if (IRcommand != "") {
            // send the request to the dock
            QVariantMap msg;
            msg.insert("type", QVariant("dock"));
            msg.insert("command", QVariant("ir_send"));
            msg.insert("code", IRcommand);
            QJsonDocument doc = QJsonDocument::fromVariant(msg);
            QString message = doc.toJson(QJsonDocument::JsonFormat::Compact);

            // send the message through the websocket api
            m_socket->sendTextMessage(message);
        }
    }
    // commands that does not have entity
    if (type == "dock") {
        if (command == RemoteDef::C_REMOTE_CHARGED) {
            QVariantMap msg;
            msg.insert("type", QVariant("dock"));
            msg.insert("command", QVariant("remote_charged"));
            QJsonDocument doc = QJsonDocument::fromVariant(msg);
            QString message = doc.toJson(QJsonDocument::JsonFormat::Compact);
            m_socket->sendTextMessage(message);
        } else if (command == RemoteDef::C_REMOTE_LOWBATTERY) {
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
