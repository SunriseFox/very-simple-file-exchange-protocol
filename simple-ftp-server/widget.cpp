#include "widget.h"
#include "ui_widget.h"

Widget::Widget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Widget)
{
    // Widget: 设置程序的主界面及服务器的收信端口

    ui->setupUi(this);

    socket = new QUdpSocket();
    bool success = socket->bind(8888);
    setWindowTitle("Server");
    if(success) ui->logger->append("(Server) listening on 8888");
    else ui->logger->append("(Server) binding port 8888 failed");
    connect(socket, &QUdpSocket::readyRead, this, &Widget::onNewDatagrams);
}

Widget::~Widget()
{
    delete ui;
}

void Widget::onNewDatagrams()
{
    // 处理客户端连接并新建线程，分发指令给对应线程
    while(socket->hasPendingDatagrams()) {
        auto datagram = socket->receiveDatagram(65535);
        const QString clientName = datagram.senderAddress().toString() + "+" + QString::number(datagram.senderPort());
        const auto dataTrimed = datagram.data().trimmed();
        qDebug() << clientName << dataTrimed;
        if(dataTrimed == "hello") {
            if (knownClients.contains(clientName)) {
                knownClients[clientName]->stopNow();
            }
            knownClients[clientName] = new ClientWorker(datagram.senderAddress(), static_cast<quint16>(datagram.senderPort()), socket);
            auto thread = new QThread;
            connect(thread, &QThread::started, knownClients[clientName], &ClientWorker::run);
            connect(knownClients[clientName], &ClientWorker::finished, thread, &QThread::quit);
            connect(knownClients[clientName], &ClientWorker::logging, this, [=](QString log){
                ui->logger->append("(" + clientName + ") " + log);
            });
            connect(thread, &QThread::finished, knownClients[clientName], &ClientWorker::deleteLater);
            knownClients[clientName]->moveToThread(thread);
            thread->start();
        } else if(dataTrimed != "X"){
            // 'X' 是 Net Cat 默认用于测试 UDP 连通性的字符串，忽略
            if (knownClients.contains(clientName)) {
                QMetaObject::invokeMethod(knownClients[clientName], [=](){
                    knownClients[clientName]->onCommand(new QByteArray(datagram.data()));
                }, Qt::QueuedConnection);
            }
            else
                socket->writeDatagram("hello\n", datagram.senderAddress(), static_cast<quint16>(datagram.senderPort()));
        }
    }
}
