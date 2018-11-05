#ifndef CLIENTWORKER_H
#define CLIENTWORKER_H

#include <QObject>
#include <QRunnable>
#include <QString>
#include <QDebug>
#include <QQueue>
#include <QHostAddress>
#include <QList>
#include <QUdpSocket>
#include <QEventLoop>
#include <QThread>
#include <QDirIterator>
#include <QDateTime>
#include <QDir>
#include <QTimer>

class ClientWorker : public QEventLoop
{
    Q_OBJECT

public:
    ClientWorker() = delete;
    ClientWorker(const QHostAddress &host, quint16 port, QUdpSocket* socket);
    ~ClientWorker() override;

public slots:
    void run();
    void stopNow();
    void onCommand(const QByteArray *data);
    void onNewCommand();

signals:
    void newCommand();
    void finished();
    void logging(const QString log);

private slots:
    void onTimerTimeout();

private:
    const QString peerName;
    QDir rootDir;
    QDir currentDir;

    QQueue<const QByteArray*> commandQueue;
    QThread * runningIn;

    QHostAddress host;
    quint16 port;
    QUdpSocket* socket;

    QByteArray lastResponse;
    QTimer timer;
    bool lastChance = false;

    bool isAuthed = false;

    int commandRetried = 0;
    int lastTimerId = -1;
    QByteArray lastPendingCommand;

    // 用于发送和接收文件
    bool ok;
    const int readBlockSize = 0xE000;
    int dataTried = 0;
    qint64 totalLength = 0;
    int sendTimerId = -1;
    QFile openedFile;

    // 用于接收文件
    bool isPendingReceiving = false;
    bool waitingRawData = false;
    int expecting = 15;
    int lastAck = 0;

    // 用于发送文件
    bool writingRawData = false;
    int bufferBegin = 0;
    QList<QByteArray> buffer;

    struct Command {
        QString command;
        std::function<bool(const QString&)> callback;
    };

    QList<Command> commands {
        { QStringLiteral("l"), std::bind(&ClientWorker::onLS, this, std::placeholders::_1) },
        { QStringLiteral("c"), std::bind(&ClientWorker::onCD, this, std::placeholders::_1) },
        { QStringLiteral("a"), std::bind(&ClientWorker::onAUTH, this, std::placeholders::_1) },
        { QStringLiteral("m"), std::bind(&ClientWorker::onMKDIR, this, std::placeholders::_1) },
        { QStringLiteral("d"), std::bind(&ClientWorker::onDELETE, this, std::placeholders::_1) },
        { QStringLiteral("q"), std::bind(&ClientWorker::onEXIT, this, std::placeholders::_1) },
        { QStringLiteral("p"), std::bind(&ClientWorker::onPUT, this, std::placeholders::_1) },
        { QStringLiteral("g"), std::bind(&ClientWorker::onGET, this, std::placeholders::_1) },
        { QStringLiteral("h"), std::bind(&ClientWorker::onHELP, this, std::placeholders::_1) },
        { QStringLiteral("?"), std::bind(&ClientWorker::onHELP, this, std::placeholders::_1) },
        { QStringLiteral("ready"), std::bind(&ClientWorker::onREADY, this, std::placeholders::_1) },
    };

    void sendCommand(int code, const QList<QString>& list, bool = false);
    void sendACKCommand(const QByteArray& command);
    void startACKTimer();
    void stopACKTimer();
    void writeToClient(const QByteArray& bytes);

    bool onLS(const QString& command);
    bool onCD(const QString& command);
    bool onAUTH(const QString& command);
    bool onMKDIR(const QString& command);
    bool onDELETE(const QString& command);
    bool onPUT(const QString& command);
    bool onGET(const QString& command);
    bool onEXIT(const QString& command);
    bool onHELP(const QString& command);
    bool onREADY(const QString& command);
    bool onNOSUCHMETHOD(const QString& command);

    std::function<bool(const QByteArray*)> beforeParse = nullptr;
    std::function<bool(const QString&)> beforeHandle = nullptr;

    void processCommand(const QByteArray* command);
    bool assertNoParams(const QString& self, const QString& command);
    QStringList getArguments(const QString& self, const QString& command, int max);

    void onWriteRawData(const QByteArray& data);
    void onReceiveRawData(const QByteArray& data);

    void acceptLastData();
    void rejectLastData();
    bool readOneBlockToBuffer();

    void debugUnknownState(const QByteArray &data);
    void retryLastCommand(bool retry = true);

    void resetAllState();
};

#endif // CLIENTWORKER_H
