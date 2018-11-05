#include "responsehandler.h"

ResponseHandler::ResponseHandler() {
    // 建立初始连接
    socket = new QUdpSocket();
    connect(socket, &QUdpSocket::readyRead, this, &ResponseHandler::onData);
    socket->bind();
    qDebug() << "? Client socket listening on" << socket->localPort();
}

void ResponseHandler::onCommand(QString command) {
    // 该函数为槽，触发槽事件时指令队列可能为空，需要判空
    if(command.isEmpty()) return;
    // 由于槽线程是 ConsoleReader 所在线程，切换到主线程。
    QMetaObject::invokeMethod(qApp, [&]{
        QStringList list = command.split(" ", QString::SkipEmptyParts);
        if (!isConnected) {
            if (list.length() == 3 && list.at(0).toLower() == "o") {
                host.setAddress(list.at(1));
                bool ok = false;
                commucationPort = port = list.at(2).toUShort(&ok);
                this->renewSocket();
                return;
            }
            qDebug() << "? use o <server> <port> to open a connection.";
        } else {
            if (list.length() == 1 && list.at(0).startsWith("q")) {
                isConnected = 0;
            } else if(list.length() == 2) {
                if(list.at(0) == "put" || list.at(0) == "get") {
                    if(openedFile.isOpen()) {
                        openedFile.close();
                    }
                    resetAllState();
                    openedFile.setFileName(list.at(1));
                    if(list.at(0) == "put") {
                        openedFile.open(QFile::ReadOnly);
                        if(openedFile.isReadable()) {
                            qDebug() << "? file opened for read.";
                            totalLength = openedFile.size() / readBlockSize + (openedFile.size() % readBlockSize != 0);
                            qDebug() << "? file length is" << totalLength;
                            for (int i = 0; i < 15; i++) {
                                if(!readOneBlockToBuffer()) break;
                            }
                            isPendingSending = true;
                            this->sendCommand((command + " " + QString::number(totalLength)).toUtf8());
                        } else {
                            qDebug() << "? failed to open file to read" << list.at(1);
                        }
                    } else if(list.at(0) == "get") {
                        openedFile.open(QFile::WriteOnly);
                        if(openedFile.isWritable()) {
                            qDebug() << "? file opened for write.";
                            isPendingReceiving = true;
                            this->sendCommand(command.toUtf8());
                        } else {
                            qDebug() << "? failed to open file to write" << list.at(1);
                        }
                    }
                    return;
                }
            }
            this->sendCommand(command.toUtf8());
        }
    });
}

void ResponseHandler::stopNow() {
    emit(finished());
}

void ResponseHandler::renewSocket()
{
    waitingHandShake = true;
    sendCommand("hello\n");
}

void ResponseHandler::onData() {
    while(socket->hasPendingDatagrams()) {
        auto datagram = socket->receiveDatagram(65535);
        if (datagram.senderAddress().toIPv4Address() != host.toIPv4Address()) {
          debugDropData(datagram);
          continue;
        }
        if(waitingHandShake) {
            if (datagram.senderPort() != port) {
                debugDropData(datagram);
                continue;
            }
            auto data = datagram.data();
            auto code = data.split(' ')[0];
            if(code != "200") return;
            waitingHandShake = false;
            isConnected = true;
            qDebug() << "? Handshake Succeeded.";
            qDebug() << datagram.data().simplified();
            acceptLastData();
        } else {
            if (datagram.senderPort() != commucationPort) {
                debugDropData(datagram);
                continue;
            }
            if(lastTimerId != -1) {
                clearTimer(lastTimerId);
            }
            auto data = datagram.data();
            if(isPendingSending) {
                auto list = data.split(' ');
                if(list.length() == 1 && list.at(0) == "ok") {
                    writingRawData = true;
                    isPendingSending = false;
                    onWriteRawData("ACK 0");
                } else {
                    isPendingSending = false;
                    debugUnknownState(datagram);
                }
            } else if(isPendingReceiving) {
                auto list = data.split(' ');
                if(list.at(0) == "size") {
                    bool ok = false;
                    qint64 size = list.at(1).toLongLong(&ok);
                    if(ok) {
                        totalLength = size - 1;
                        waitingRawData = true;
                        isPendingReceiving = false;
                        sendCommand("ready");
                    }
                } else {
                    isPendingReceiving = false;
                    debugUnknownState(datagram);
                }
            } else if(writingRawData) {
                onWriteRawData(data);
            } else if(waitingRawData) {
                onReceiveRawData(data);
            } else {
                // 不显示过长的指令
                if(data.length() < 50)
                    qDebug() << data.simplified();
                acceptLastData();
            }
        }
    }
}

void ResponseHandler::onWriteRawData(const QByteArray &data)
{
    int begin = data.indexOf(" ");
    bool ok = false;
    int ack = data.mid(begin).toInt(&ok);
    qDebug() << "? got ack" << ack;
    if(ack == totalLength) {
        writingRawData = false;
//        sendCommand("no more", false);
        qDebug() << "? sending data finished";
        resetAllState();
        return;
    }
    if(ok && ack >= bufferBegin && ack <= bufferBegin + buffer.length() + 1) {
        while (bufferBegin < ack) {
            buffer.pop_front();
            bufferBegin++;
        }
        while (buffer.length() < 15) {
            if(!readOneBlockToBuffer())
                break;
        }
        for(int i = 0; i < buffer.length(); i++) {
            qDebug() << "? sending block" << bufferBegin + i;
            // 触发确认重传机制
            QThread::msleep(100);
            sendCommand(QByteArray::number(bufferBegin + i) + " " + buffer.at(i), false);
        }
        qDebug() << "? group of data sent";
        sendTimerId = startTimer(5000);
        return;
    }
    debugUnknownState(data);
}

void ResponseHandler::onReceiveRawData(const QByteArray &data)
{
    int begin = data.indexOf(" ");
    bool ok = false;
    int block = data.left(begin).toInt(&ok);
    qDebug() << "? got block" << block;
    if(ok && block == lastAck && block <= totalLength) {
        if(sendTimerId >= 0)
            clearTimer(sendTimerId);
        dataTried = 0;
        lastAck++;
        if(openedFile.isWritable()) {
            openedFile.write(data.mid(begin + 1));
        }
        acceptLastData();
        if(block == totalLength) {
            waitingRawData = false;
            openedFile.close();
            qDebug() << "? file transfer finished";
            sendCommand(QByteArray("ACK ") + QByteArray::number(block + 1), false);
        } else {
            expecting--;
            if(expecting == 0) {
                expecting = 15;
                sendCommand(QByteArray("ACK ") + QByteArray::number(lastAck), false);
            }
        }
        sendTimerId = startTimer(5000);
    } else {
        onSendReceiveTimeout(-block);
    }
}

void ResponseHandler::acceptLastData()
{
    if(lastTimerId != -1)
        clearTimer(lastTimerId);
    commandRetried = 0;
    lastPendingCommand.clear();
}

void ResponseHandler::rejectLastData()
{
    resetAllState();
    socket->deleteLater();
    socket = new QUdpSocket();
    connect(socket, &QUdpSocket::readyRead, this, &ResponseHandler::onData);
    socket->bind();
    qDebug() << "? Client socket listening on" << socket->localPort();
    isConnected = waitingHandShake = false;
    acceptLastData();
}

bool ResponseHandler::readOneBlockToBuffer()
{
    QByteArray bytes = openedFile.read(readBlockSize);
    if(bytes.isEmpty() || bytes.isNull()) return false;
    buffer.push_back(bytes);
    if(bytes.length() != readBlockSize)
        return false;
    return true;
}

void ResponseHandler::debugDropData(const QNetworkDatagram &datagram)
{
    qDebug() << "? dropped unknown data from"
             << datagram.senderAddress()
             << datagram.senderPort() << datagram.data();
}

void ResponseHandler::debugUnknownState(const QNetworkDatagram &datagram, bool retry)
{
    qDebug() << "? received a datagram that should not be here"
             << datagram.senderAddress()
             << datagram.senderPort() << datagram.data();
    if(retry == false) return;
    if(commandRetried == 2) {
        qDebug() << "? failed to get current state, reset.";
        rejectLastData();
        return;
    } else {
        commandRetried ++;
        retryLastCommand(false);
    }
}

void ResponseHandler::retryLastCommand(bool retry)
{
    if(lastPendingCommand.isNull()) {
        acceptLastData();
        return;
    };
    if(commandRetried == 2) {
        qDebug() << "? failed to send command, reset.";
        rejectLastData();
        return;
    }
    commandRetried ++;
    qDebug() << "? retrying command" << lastPendingCommand << commandRetried;
    sendCommand(lastPendingCommand, retry);
}

void ResponseHandler::clearTimer(int &timer)
{
    if(timer != -1)
        killTimer(timer);
    timer = -1;
}

void ResponseHandler::resetAllState()
{
    openedFile.close();
    dataTried = 0;
    totalLength = 0;
    sendTimerId = -1;
    isPendingReceiving = false;
    waitingRawData = false;
    writingRawData = false;
    expecting = 15;
    lastAck = 0;
    bufferBegin = 0;
    buffer.clear();
}

ResponseHandler::~ResponseHandler() {
    socket->deleteLater();
}

void ResponseHandler::sendCommand(const QByteArray &array, bool retry) {
    if(lastTimerId >= 0) {
        qDebug() << "? last command is still pending...";
        return;
    }
    if(retry) {
        lastTimerId = this->startTimer(5000);
        lastPendingCommand = array;
    }
    socket->writeDatagram(array, host, commucationPort);
}

void ResponseHandler::timerEvent(QTimerEvent *event)
{
    Q_UNUSED(event);
    if(writingRawData || waitingRawData) {
        onSendReceiveTimeout(event->timerId());
        return;
    }
    if(lastTimerId == event->timerId()) {
        clearTimer(lastTimerId);
        retryLastCommand();
    }
}

void ResponseHandler::onSendReceiveTimeout(int timerId)
{
    if(sendTimerId >= 0)
        clearTimer(sendTimerId);
    if(waitingRawData) {
        qDebug() << "? unknown block, expecting" << lastAck << "got" << (timerId >= 0 ? "timeout" : QString::number(-timerId));
        if(dataTried == 2) {
            qDebug() << "? file transfer failed";
            rejectLastData();
            return;
        }
        expecting = 15;
        acceptLastData();
        dataTried ++;
        sendCommand(QByteArray("ACK ") + QByteArray::number(lastAck));
        return;
    }
    qDebug() << "? SendReceiveTimeout";
    rejectLastData();
}
