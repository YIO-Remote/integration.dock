#ifndef DOCK_H
#define DOCK_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QVariant>
#include <QtWebSockets/QWebSocket>
#include <QTimer>
#include <QThread>

#include "../remote-software/sources/integrations/integration.h"
#include "../remote-software/sources/integrations/integrationinterface.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// DOCK FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Dock : public IntegrationInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "YIO.IntegrationInterface" FILE "dock.json")
    Q_INTERFACES(IntegrationInterface)

public:
    explicit Dock() {}

    void create                     (const QVariantMap& config, QObject *entities, QObject *notifications, QObject* api, QObject *configObj) override;

public slots:
    void test() { qDebug() << "HELLLO"; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// DOCK CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class DockBase : public Integration
{
    Q_OBJECT

public:
    explicit DockBase(QObject *parent);

    Q_INVOKABLE void setup  	    (const QVariantMap& config, QObject *entities, QObject *notifications, QObject* api, QObject *configObj);
    Q_INVOKABLE void connect	    ();
    Q_INVOKABLE void disconnect	    ();

signals:
    void connectSignal              ();
    void disconnectSignal           ();
    void sendCommandSignal          (const QString& type, const QString& entity_id, const QString& command, const QVariant& param);


public slots:
    void sendCommand                (const QString& type, const QString& entity_id, const QString& command, const QVariant& param);
    void stateHandler               (int state);

private:
    void updateEntity               (const QString& entity_id, const QVariantMap& attr) {}

    QThread                         m_thread;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// DOCK THREAD CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class DockThread : public QObject
{
    Q_OBJECT

public:
    DockThread                      (const QVariantMap &config, QObject *entities, QObject *notifications, QObject *api, QObject *configObj);

    QString                         m_friendly_name;

signals:
    void stateChanged               (int state);

public slots:
    void connect                    ();
    void disconnect                 ();

    void sendCommand                (const QString& type, const QString& entity_id, const QString& command, const QVariant& param);

    void onTextMessageReceived	    (const QString &message);
    void onStateChanged             (QAbstractSocket::SocketState state);
    void onError                    (QAbstractSocket::SocketError error);

    void onTimeout                  ();


private:
    void webSocketSendCommand	    (const QString& domain, const QString& service, const QString& entity_id, QVariantMap *data);
    void updateEntity               (const QString& entity_id, const QVariantMap& attr);
    void setState                   (int state);
    QString findIRCode              (const QString& feature, QVariantList& list);

    EntitiesInterface*              m_entities;
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

    int                             m_state = 0;

};


#endif // DOCK_H
