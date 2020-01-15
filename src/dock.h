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

#pragma once

#include <QColor>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QtWebSockets/QWebSocket>

#include "yio-interface/configinterface.h"
#include "yio-interface/notificationsinterface.h"
#include "yio-interface/plugininterface.h"
#include "yio-interface/yioapiinterface.h"
#include "yio-plugin/integration.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// DOCK FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class DockPlugin : public PluginInterface {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "YIO.PluginInterface" FILE "dock.json")
  Q_INTERFACES(PluginInterface)

 public:
  DockPlugin() : m_log("dock") {}

  void create(const QVariantMap& config, QObject* entities,
              QObject* notifications, QObject* api,
              QObject* configObj) override;
  void setLogEnabled(QtMsgType msgType, bool enable) override {
    m_log.setEnabled(msgType, enable);
  }

 private:
  QLoggingCategory m_log;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// DOCK CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Dock : public Integration {
  Q_OBJECT

 public:
  explicit Dock(const QVariantMap& config, const QVariantMap& mdns,
                QObject* entities, QObject* notifications, QObject* api,
                QObject* configObj,
                QLoggingCategory& log);  // NOLINT can't use const

  Q_INVOKABLE void connect() override;
  Q_INVOKABLE void disconnect() override;
  Q_INVOKABLE void enterStandby() override;
  Q_INVOKABLE void leaveStandby() override;
  Q_INVOKABLE void sendCommand(const QString& type, const QString& entity_id,
                               int command, const QVariant& param) override;

  QString m_friendly_name;

 public slots:  // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
  void onTextMessageReceived(const QString& message);
  void onStateChanged(QAbstractSocket::SocketState state);
  void onError(QAbstractSocket::SocketError error);

  void onTimeout();

 private:
  void webSocketSendCommand(const QString& domain, const QString& service,
                            const QString& entity_id, QVariantMap* data);
  void updateEntity(const QString& entity_id, const QVariantMap& attr);
  QStringList findIRCode(const QString& feature, const QVariantList& list);

  NotificationsInterface* m_notifications;
  YioAPIInterface* m_api;
  ConfigInterface* m_config;

  QString m_id;

  QString m_ip;
  QString m_token;
  QWebSocket* m_socket;
  QTimer* m_websocketReconnect;
  int m_tries;
  bool m_userDisconnect = false;

  QLoggingCategory& m_log;

  int m_heartbeatCheckInterval = 30000;
  QTimer* m_heartbeatTimer = new QTimer(this);
  void onHeartbeat();
  QTimer* m_heartbeatTimeoutTimer = new QTimer(this);
  void onHeartbeatTimeout();
};
