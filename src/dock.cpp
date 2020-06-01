/******************************************************************************
 *
 * Copyright (C) 2020 Markus Zehnder <business@markuszehnder.ch>
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

#include "dock.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "math.h"
#include "yio-interface/entities/entitiesinterface.h"
#include "yio-interface/entities/entityinterface.h"
#include "yio-interface/entities/remoteinterface.h"

DockPlugin::DockPlugin() : Plugin("dock", NO_WORKER_THREAD) {}

Integration *DockPlugin::createIntegration(const QVariantMap &config, EntitiesInterface *entities,
                                           NotificationsInterface *notifications, YioAPIInterface *api,
                                           ConfigInterface *configObj) {
    qCInfo(m_logCategory) << "Creating YIO Dock integration plugin" << PLUGIN_VERSION;

    return new Dock(config, entities, notifications, api, configObj, this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// DOCK CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Dock::Dock(const QVariantMap &config, EntitiesInterface *entities, NotificationsInterface *notifications,
           YioAPIInterface *api, ConfigInterface *configObj, Plugin *plugin)
    : Integration(config, entities, notifications, api, configObj, plugin) {
    for (QVariantMap::const_iterator iter = config.begin(); iter != config.end(); ++iter) {
        if (iter.key() == Integration::OBJ_DATA) {
            QVariantMap map = iter.value().toMap();
            m_hostname      = map.value(Integration::KEY_DATA_IP).toString();
        }
    }

    m_url = QString("ws://").append(m_hostname).append(":946");

    qRegisterMetaType<QAbstractSocket::SocketState>();

    m_entities = entities;

    m_wsReconnectTimer = new QTimer();
    m_wsReconnectTimer->setSingleShot(true);
    m_wsReconnectTimer->setInterval(2000);
    m_wsReconnectTimer->stop();

    m_webSocket = new QWebSocket;
    m_webSocket->setParent(this);

    QObject::connect(m_webSocket, &QWebSocket::textMessageReceived, this, &Dock::onTextMessageReceived);
    QObject::connect(m_webSocket, static_cast<void (QWebSocket::*)(QAbstractSocket::SocketError)>(&QWebSocket::error),
                     this, &Dock::onError);
    QObject::connect(m_webSocket, &QWebSocket::stateChanged, this, &Dock::onStateChanged);

    QObject::connect(m_wsReconnectTimer, &QTimer::timeout, this, &Dock::onTimeout);

    // set up timer to check heartbeat
    m_heartbeatTimer->setInterval(m_heartbeatCheckInterval);
    QObject::connect(m_heartbeatTimer, &QTimer::timeout, this, &Dock::onHeartbeat);

    // set up heartbeat timeout timer
    m_heartbeatTimeoutTimer->setSingleShot(true);
    m_heartbeatTimeoutTimer->setInterval(m_heartbeatCheckInterval / 2);
    QObject::connect(m_heartbeatTimeoutTimer, &QTimer::timeout, this, &Dock::onHeartbeatTimeout);
}

void Dock::onTextMessageReceived(const QString &message) {
    QJsonParseError parseerror;
    QJsonDocument   doc = QJsonDocument::fromJson(message.toUtf8(), &parseerror);
    if (parseerror.error != QJsonParseError::NoError) {
        qCWarning(m_logCategory) << "JSON error : " << parseerror.errorString();
        return;
    }
    QVariantMap map = doc.toVariant().toMap();

    QString m = map.value("error").toString();
    if (m.length() > 0) {
        qCWarning(m_logCategory) << "error : " << m;
    }

    QString type = map.value("type").toString();
    // int id = map.value("id").toInt();

    if (type == "auth_required") {
        QString auth = QString("{ \"type\": \"auth\", \"token\": \"%1\" }\n").arg(m_token);
        m_webSocket->sendTextMessage(auth);
    }

    if (type == "auth_ok") {
        qCInfo(m_logCategory) << "Connection successful:" << friendlyName() << m_hostname;
        setState(CONNECTED);
        m_tries = 0;
        m_heartbeatTimer->start();
    }

    // heartbeat
    if (type == "dock" && map.value("message").toString() == "pong") {
        qCDebug(m_logCategory) << "Got heartbeat from dock!";
        m_heartbeatTimeoutTimer->stop();
    }
}

void Dock::onStateChanged(QAbstractSocket::SocketState state) {
    if (state == QAbstractSocket::UnconnectedState && !m_userDisconnect) {
        setState(DISCONNECTED);
        m_wsReconnectTimer->start();
    }
}

void Dock::onError(QAbstractSocket::SocketError error) {
    qCWarning(m_logCategory) << error;
    m_webSocket->close();
    setState(DISCONNECTED);
    m_wsReconnectTimer->start();
}

void Dock::onTimeout() {
    qCDebug(m_logCategory) << "onTimeout!";

    if (m_tries == 3) {
        m_wsReconnectTimer->stop();
        qCCritical(m_logCategory) << "Cannot connect to docking station: retried 3 times connecting to" << m_hostname;

        QObject *param = this;
        m_notifications->add(
            true, tr("Cannot connect to ").append(friendlyName()).append("."), tr("Reconnect"),
            [](QObject *param) {
                Integration *i = qobject_cast<Integration *>(param);
                i->connect();
            },
            param);

        disconnect();
        m_tries = 0;
    } else {
        if (m_state != CONNECTING) {
            setState(CONNECTING);
        }
        qCInfo(m_logCategory) << "Reconnection attempt" << m_tries + 1 << "to docking station:" << m_url;
        m_webSocket->open(QUrl(m_url));

        m_tries++;
    }
}

void Dock::onLowBattery() { sendCommand("dock", "", RemoteDef::C_REMOTE_LOWBATTERY, ""); }

void Dock::connect() {
    qCDebug(m_logCategory) << "connect!";

    m_userDisconnect = false;

    setState(CONNECTING);

    // reset the reconnnect trial variable
    m_tries = 0;

    // turn on the websocket connection
    if (m_webSocket->isValid()) {
        m_webSocket->close();
    }

    qCDebug(m_logCategory) << "Connecting to docking station:" << m_url;
    m_webSocket->open(QUrl(m_url));
}

void Dock::disconnect() {
    m_userDisconnect = true;
    qCDebug(m_logCategory) << "Disconnecting from docking station";

    // stop heartbeat pings
    m_heartbeatTimer->stop();
    m_heartbeatTimeoutTimer->stop();

    qCDebug(m_logCategory) << "Stopped heartbeat timers";

    // turn of the reconnect try
    m_wsReconnectTimer->stop();

    qCDebug(m_logCategory) << "Stopped reconnect timer";

    // turn off the socket
    m_webSocket->close();

    qCDebug(m_logCategory) << "Closed websocket";

    setState(DISCONNECTED);
}

void Dock::enterStandby() {
    qCDebug(m_logCategory) << "Entering standby";
    m_heartbeatTimer->stop();
    m_heartbeatTimeoutTimer->stop();
    qCDebug(m_logCategory) << "Stopped heartbeat timers";
}

void Dock::leaveStandby() {
    qCDebug(m_logCategory) << "Leaving standby";
    m_heartbeatTimer->start();
    qCDebug(m_logCategory) << "Started heartbeat timer";
}

void Dock::sendCommand(const QString &type, const QString &entityId, int command, const QVariant &param) {
    Q_UNUSED(param)
    qCDebug(m_logCategory) << "Sending command" << type << entityId << command;

    if (type == "remote") {
        // get the remote enityt from the entity database
        EntityInterface *entity          = m_entities->getEntityInterface(entityId);
        RemoteInterface *remoteInterface = static_cast<RemoteInterface *>(entity->getSpecificInterface());

        // get all the commands the entity can do (IR codes)
        QVariantList commands = remoteInterface->commands();

        // find the IR code that matches the command we got from the UI
        QString     commandText = entity->getCommandName(command);
        QStringList IRcommand   = findIRCode(commandText, commands);

        if (IRcommand[0] != "") {
            // send the request to the dock
            QVariantMap msg;
            msg.insert("type", QVariant("dock"));
            msg.insert("command", QVariant("ir_send"));
            msg.insert("code", IRcommand[0]);
            msg.insert("format", IRcommand[1]);
            QJsonDocument doc     = QJsonDocument::fromVariant(msg);
            QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);

            // send the message through the websocket api
            m_webSocket->sendTextMessage(message);
        }
    }

    // commands that does not have entity
    if (type == "dock") {
        if (command == RemoteDef::C_REMOTE_CHARGED) {
            QVariantMap msg;
            msg.insert("type", QVariant("dock"));
            msg.insert("command", QVariant("remote_charged"));
            QJsonDocument doc     = QJsonDocument::fromVariant(msg);
            QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);
            m_webSocket->sendTextMessage(message);
        } else if (command == RemoteDef::C_REMOTE_LOWBATTERY) {
            QVariantMap msg;
            msg.insert("type", QVariant("dock"));
            msg.insert("command", QVariant("remote_lowbattery"));
            QJsonDocument doc     = QJsonDocument::fromVariant(msg);
            QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);
            m_webSocket->sendTextMessage(message);
        }
    }
}

QStringList Dock::findIRCode(const QString &feature, const QVariantList &list) {
    QStringList r;

    for (int i = 0; i < list.length(); i++) {
        QVariantMap map = list[i].toMap();
        if (map.value("button_map").toString() == feature) {
            r.append(map.value("code").toString());
            r.append(map.value("format").toString());
        }
    }

    if (r.length() == 0) {
        r.append("");
    }

    return r;
}

void Dock::onHeartbeat() {
    qCDebug(m_logCategory) << "Sending heartbeat request";
    QString msg = QString("{ \"type\": \"dock\", \"command\": \"ping\" }\n");
    m_webSocket->sendTextMessage(msg);
    qCDebug(m_logCategory) << "Started heartbeat timeout timer";
    m_heartbeatTimeoutTimer->start();
}

void Dock::onHeartbeatTimeout() {
    qCDebug(m_logCategory) << "Heartbeat timeout!";
    onTimeout();
}
