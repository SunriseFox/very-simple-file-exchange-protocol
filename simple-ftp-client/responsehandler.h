#ifndef RESPONSEHANDLER_H
#define RESPONSEHANDLER_H

#include <QObject>
#include <QEventLoop>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QThread>
#include <QTextStream>
#include <QCoreApplication>
#include <QTimerEvent>
#include <QList>
#include <QFile>
#include <QTimer>

class ResponseHandler : public QObject {

    Q_OBJECT

    bool isConnected = false;
    bool waitingHandShake = false;

    QHostAddress host;
    quint16 port;

    QUdpSocket* socket = nullptr;
    quint16 commucationPort;

    int commandRetried = 0;
    int lastTimerId = -1;
    QByteArray lastPendingCommand;

    // 用于发送和接收文件
    const int readBlockSize = 0xE000;
    int dataTried = 0;
    qint64 totalLength = 0;
    int sendTimerId = -1;
    QFile openedFile;

    // 用于接收文件
    bool waitingRawData = false;
    bool isPendingReceiving = false;
    int expecting = 15;
    int lastAck = 0;

    // 用于发送文件
    bool writingRawData = false;
    bool isPendingSending = false;
    int bufferBegin = 0;
    QList<QByteArray> buffer;

public:
    ResponseHandler();
    ~ResponseHandler() override;
    void stopNow();
    void renewSocket();
    void sendCommand(const QByteArray& array, bool retry = true);
signals:
    void finished();

protected:
    void timerEvent(QTimerEvent* event) override;
    void onSendReceiveTimeout(int timerId);

public slots:
    void onCommand(QString command);

private slots:
    void onData();
    void onWriteRawData(const QByteArray& data);
    void onReceiveRawData(const QByteArray& data);

private:
    void acceptLastData();
    void rejectLastData();
    bool readOneBlockToBuffer();

    void debugDropData(const QNetworkDatagram& datagram);
    void debugUnknownState(const QNetworkDatagram& datagram, bool retry = true);
    void retryLastCommand(bool retry = true);

    void clearTimer(int& timer);
    void resetAllState();
};

#endif // RESPONSEHANDLER_H
