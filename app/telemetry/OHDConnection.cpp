#include "OHDConnection.h"

#include "qopenhdmavlinkhelper.hpp"
#include "telemetry/openhd_defines.hpp"

#include <mavsdk/log_callback.h>

OHDConnection::OHDConnection(QObject *parent,bool useTcp):QObject(parent),USE_TCP(useTcp)
{
    if(USE_TCP){
        tcpClientSock = new QTcpSocket(this);
        connect(tcpClientSock, &QTcpSocket::readyRead, this, &OHDConnection::tcpReadyRead);
        connect(tcpClientSock, &QTcpSocket::connected, this, &OHDConnection::tcpConnected);
        connect(tcpClientSock, &QTcpSocket::disconnected, this, &OHDConnection::tcpDisconnected);
    }else{
        udpSocket = new QUdpSocket(this);
        //auto bindStatus = udpSocket->bind(QHostAddress::Any,QOPENHD_GROUND_CLIENT_UDP_PORT_IN);
        // this is how it is done int QGroundCOntroll:
        //  _telemetrySocket->bind(QHostAddress::LocalHost, 0, QUdpSocket::ShareAddress);
        //auto bindStatus = udpSocket->bind(QHostAddress::LocalHost,QOPENHD_GROUND_CLIENT_UDP_PORT_IN);
        auto bindStatus = udpSocket->bind(QHostAddress::Any,QOPENHD_GROUND_CLIENT_UDP_PORT_IN);
        if (!bindStatus) {
            qDebug() <<"Cannot bind UDP Socket";
        }
        connect(udpSocket, &QUdpSocket::readyRead, this, &OHDConnection::udpReadyRead);
    }
    start();
    /*mavsdk=std::make_shared<mavsdk::Mavsdk>();
    mavsdk::log::subscribe([](mavsdk::log::Level level,   // message severity level
                              const std::string& message, // message text
                              const std::string& file,    // source file from which the message was sent
                              int line) {                 // line number in the source file
      // process the log message in a way you like
      qDebug()<<message.c_str();
      // returning true from the callback disables printing the message to stdout
      return level < mavsdk::log::Level::Warn;
    });
    mavsdk::ConnectionResult connection_result = mavsdk->add_udp_connection(QOPENHD_GROUND_CLIENT_UDP_PORT_IN);
    std::stringstream ss;
    ss<<"MAVSDK connection: " << connection_result;
    qDebug()<<ss.str().c_str();
    mavsdk->subscribe_on_new_system([this]() {
        qDebug()<<"System found";
        auto system = this->mavsdk->systems().back();
        if(system->get_system_id()==OHD_SYS_ID_GROUND){
            qDebug()<<"Found OHD Ground station";
            systemOhdGround=system;
        }else if(system->get_system_id()==OHD_SYS_ID_AIR){
            qDebug()<<"Found OHD AIR station";
        }
        qDebug()<<"Sys id:"<<system->get_system_id();
        if(system->get_system_id()==OHD_SYS_ID_GROUND){
            passtroughOhdGround=std::make_shared<mavsdk::MavlinkPassthrough>(system);
            qDebug()<<"XX:"<<passtroughOhdGround->get_target_sysid();
            passtroughOhdGround->subscribe_message_async(MAVLINK_MSG_ID_ONBOARD_COMPUTER_STATUS,[](const mavlink_message_t& msg){
                qDebug()<<"Got MAVLINK_MSG_ID_ONBOARD_COMPUTER_STATUS";
            });
            passtroughOhdGround->intercept_incoming_messages_async([this](mavlink_message_t& msg){
                //qDebug()<<"Intercept:Got message"<<msg.msgid;
                if(this->callback!=nullptr){
                     this->callback(msg);
                }
                return true;
            });
            passtroughOhdGround->intercept_outgoing_messages_async([](mavlink_message_t& msg){
                //qDebug()<<"Intercept:send message"<<msg.msgid;
                return true;
            });
            paramOhdGround=std::make_unique<mavsdk::Param>(system);
        }else if(system->has_autopilot()){
            telemetryFC=std::make_unique<mavsdk::Telemetry>(system);
            auto res=telemetryFC->set_rate_attitude(60);
            std::stringstream ss;
            ss<<res;
            qDebug()<<"Set rate result:"<<ss.str().c_str();
        }
    });
    start();*/
}

void OHDConnection::start(){
    // we only need a reconnection timer for tcp, in case of UDP it is safe to assume that
    // we never loose the port (data might or might not arrive at it though)
    if(USE_TCP){
        reconnectTimer = new QTimer(this);
        connect(reconnectTimer, &QTimer::timeout, this, &OHDConnection::reconnectIfDisconnected);
        reconnectTimer->start(1000);
    }
    // the heartbeat timer runs regardless of TCP or UDP
    heartbeatTimer = new QTimer(this);
    connect(heartbeatTimer, &QTimer::timeout, this, &OHDConnection::onHeartbeat);
    heartbeatTimer->start(1000);
}

void OHDConnection::stop(){
    if(USE_TCP){
        reconnectTimer->stop();
        tcpClientSock->disconnectFromHost();
    }
    heartbeatTimer->stop();
}

// called by the reconnect timer
void OHDConnection::reconnectIfDisconnected(){
    if (tcpClientSock->state() == QAbstractSocket::UnconnectedState) {
        qDebug() << "OHDConnection::reconnectIfDisconnected";
        tcpClientSock->connectToHost(QOPENHD_GROUND_CLIENT_TCP_ADDRESS, QOPENHD_GROUND_CLIENT_TCP_PORT);
    }
}

void OHDConnection::registerNewMessageCalback(MAV_MSG_CALLBACK cb){
    if(callback!= nullptr){
        // this might be a common programming mistake - you can only register one callback here
         qDebug()<<"OHDConnection::Overwriting already existing callback\n";
    }
    callback=std::move(cb);
}

void OHDConnection::sendMessage(mavlink_message_t msg){
    const auto sys_id=QOpenHDMavlinkHelper::getSysId();
    const auto comp_id=QOpenHDMavlinkHelper::getCompId();
    if(msg.sysid!=sys_id){
        // probably a programming error, the message was not packed with the right sys id
        qDebug()<<"WARN Sending message with sys id:"<<msg.sysid<<" instead of"<<sys_id;
    }
    if(msg.compid!=comp_id){
        // probably a programming error, the message was not packed with the right comp id
        qDebug()<<"WARN Sending message with comp id:"<<msg.compid<<" instead of"<<comp_id;
    }
    /*if(mavsdk!=nullptr){
        if(passtroughOhdGround!=nullptr){
            passtroughOhdGround->send_message(msg);
        }else{
            qDebug()<<"MAVSDK ground unit not discovered";
        }
        return;
    }*/
    const auto buf=QOpenHDMavlinkHelper::mavlinkMessageToSendBuffer(msg);
    sendData(buf.data(),buf.size());
}

void OHDConnection::parseNewData(const uint8_t* data, int data_len){
    //qDebug()<<"OHDConnection::parseNewData"<<data_len;
    mavlink_message_t msg;
    for(int i=0;i<data_len;i++){
        uint8_t res = mavlink_parse_char(MAVLINK_COMM_0, (uint8_t)data[i], &msg, &receiveMavlinkStatus);
        if (res) {
            //qDebug()<<"Got new mavlink message";
            lastMavlinkMessage=std::chrono::steady_clock::now();
            if(callback!= nullptr){
                //qDebug()<<"Forwarding new mavlink message";
                callback(msg);
            }else{
                qDebug()<<"OHDConnection::No callback set,did you forget to add it ?";
            }
        }
    }
}

void OHDConnection::tcpReadyRead() {
    qDebug() << "OHDConnection::tcpReadyRead";
    QByteArray data = tcpClientSock->readAll();
    parseNewData((uint8_t*)data.data(),data.size());
}

void OHDConnection::tcpConnected() {
    qDebug() << "OHDConnection::tcpConnected";
}

void OHDConnection::tcpDisconnected() {
    qDebug() << "OHDConnection::tcpDisconnected";
    reconnectIfDisconnected();
}

void OHDConnection::udpReadyRead() {
    //qDebug() << "OHDConnection::udpReadyRead";
    QByteArray datagram;
    while (udpSocket->hasPendingDatagrams()) {
        datagram.resize(int(udpSocket->pendingDatagramSize()));
        QHostAddress _groundAddress;
        quint16 groundPort;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &_groundAddress, &groundPort);
        parseNewData((uint8_t*)datagram.data(),datagram.size());
        foundSenderIp=_groundAddress.toString();
        foundSenderPort=groundPort;
        foundSenderHostAddress=_groundAddress;
        //qDebug()<<"foundSenderIp:"<<foundSenderIp<<" foundSenderPort:"<<foundSenderPort;
    }
    //QByteArray data = udpSocket->readAll();
    //parseNewData((uint8_t*)data.data(),data.size());
}

void OHDConnection::sendData(const uint8_t* data,int data_len){
    //qDebug() << "OHDConnection::sendData";
    if(USE_TCP){
        if (tcpClientSock->state() == QAbstractSocket::ConnectedState) {
            tcpClientSock->write((char*)data, data_len);
        }
    }else{
        if(foundSenderIp.isEmpty() || foundSenderPort==-1){
            qDebug()<<"Cannot send data, sender ip and port unknown"<<foundSenderIp<<":"<<foundSenderPort;
            return;
        }
        if(udpSocket==nullptr){
            qDebug()<<"Error send data udp socket null";
            return;
        }
        //udpSocket->writeDatagram((char*)data, data_len, QHostAddress(QOPENHD_GROUND_CLIENT_UDP_ADDRESS), QOPENHD_GROUND_CLIENT_UDP_PORT_OUT);
        udpSocket->writeDatagram((char*)data, data_len, foundSenderHostAddress, foundSenderPort); //QHostAddress(foundSenderIp)
    }
}

void OHDConnection::onHeartbeat(){
    //qDebug() << "OHDConnection::onHeartbeat";
    mavlink_message_t msg;
    // values from QGroundControll
    mavlink_msg_heartbeat_pack(QOpenHDMavlinkHelper::getSysId(),QOpenHDMavlinkHelper::getCompId(), &msg,MAV_TYPE_GCS,            // MAV_TYPE
                               MAV_AUTOPILOT_INVALID,   // MAV_AUTOPILOT
                               MAV_MODE_MANUAL_ARMED,   // MAV_MODE
                               0,                       // custom mode
                               MAV_STATE_ACTIVE);       // MAV_STATE
    sendMessage(msg);
}
