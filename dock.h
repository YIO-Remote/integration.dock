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

#ifndef DOCK_H
#define DOCK_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QVariant>
#include <QtWebSockets/QWebSocket>
#include <QTimer>
#include <QThread>
#include <QLoggingCategory>

#include "../remote-software/sources/integrations/integration.h"
#include "../remote-software/sources/integrations/plugininterface.h"
#include "../remote-software/sources/entities/entitiesinterface.h"
#include "../remote-software/sources/entities/entityinterface.h"
#include "../remote-software/sources/entities/remoteinterface.h"
#include "../remote-software/sources/notificationsinterface.h"
#include "../remote-software/sources/yioapiinterface.h"
#include "../remote-software/sources/configinterface.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// DOCK FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class DockPlugin : public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "YIO.PluginInterface" FILE "dock.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit DockPlugin() :
        m_log("dock")
    {}

    void create                     (const QVariantMap& config, QObject *entities, QObject *notifications, QObject* api, QObject *configObj) override;
    void setLogEnabled              (QtMsgType msgType, bool enable) override
    {
        m_log.setEnabled(msgType, enable);
    }
private:
    QLoggingCategory    m_log;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// DOCK CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Dock : public Integration
{
    Q_OBJECT

public:
    explicit Dock             (const QVariantMap &config, const QVariantMap &mdns, QObject *entities, QObject *notifications, QObject *api, QObject *configObj, QLoggingCategory& log);
    virtual ~Dock() {}

    Q_INVOKABLE void connect	    ();
    Q_INVOKABLE void disconnect	    ();
    Q_INVOKABLE void sendCommand    (const QString& type, const QString& entity_id, int command, const QVariant& param);

    QString                         m_friendly_name;

public slots:
    void onTextMessageReceived	    (const QString &message);
    void onStateChanged             (QAbstractSocket::SocketState state);
    void onError                    (QAbstractSocket::SocketError error);

    void onTimeout                  ();


private:
    void webSocketSendCommand	    (const QString& domain, const QString& service, const QString& entity_id, QVariantMap *data);
    void updateEntity               (const QString& entity_id, const QVariantMap& attr);
    QString findIRCode              (const QString& feature, QVariantList& list);

    NotificationsInterface*         m_notifications;
    YioAPIInterface*                m_api;
    ConfigInterface*                m_config;

    QString                         m_id;

    QString                         m_ip;
    QString                         m_token;
    QWebSocket*                     m_socket;
    QTimer*                         m_websocketReconnect;
    int                             m_tries;
    bool                            m_userDisconnect = false;

    QLoggingCategory&               m_log;
};


#endif // DOCK_H
