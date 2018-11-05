#include "clientworker.h"

ClientWorker::ClientWorker
(const QHostAddress &host, quint16 port, QUdpSocket* socket)
    : peerName(host.toString() + "+" + QString::number(port)),
      host(host),
      port(port),
      socket(socket)
{
    timer.setSingleShot(true);
    timer.setInterval(5000);
    connect(&timer, &QTimer::timeout, this, &ClientWorker::onTimerTimeout);
    connect(this, &ClientWorker::newCommand, this, &ClientWorker::onNewCommand);
}

ClientWorker::~ClientWorker()
{
    this->exit();
}

void ClientWorker::run()
{
    sendCommand(200, {"vftp.vampire.rip greets", peerName});
    this->exec();
    return;
}

void ClientWorker::stopNow()
{
    this->exit();
    emit(finished());
}

void ClientWorker::onCommand(const QByteArray* data) {
    commandQueue.push_back(data);
    emit(newCommand());
}

void ClientWorker::onNewCommand()
{
    while (!commandQueue.empty()) {
        processCommand(commandQueue.front());
        delete commandQueue.front();
        commandQueue.pop_front();
    }
}

void ClientWorker::onTimerTimeout()
{
    if (lastChance) {
        this->stopNow();
    } else {
        lastChance = true;
        writeToClient(lastResponse);
        timer.singleShot(5000, this, &ClientWorker::onTimerTimeout);
    }
}

void ClientWorker::sendCommand(int code, const QList<QString> &list, bool hasNext)
{
    QString message = list.join(" \x00 ");
    qDebug() << "< " << message;
    emit(logging("< " + message));
    writeToClient(QByteArray::number(code) + (list.length() ? ((hasNext? "-" :" ") + message.toLatin1()) : "") + "\n");
}

void ClientWorker::sendACKCommand(const QByteArray& command)
{
    qDebug() << "<" << command;
    // 不显示过长的指令
    if(command.length() < 50)
        emit(logging("< " + command));
    writeToClient(command);
    lastResponse = command;

}

void ClientWorker::startACKTimer()
{
    timer.start();
}

void ClientWorker::stopACKTimer()
{
    timer.stop();
}

void ClientWorker::writeToClient(const QByteArray &bytes)
{
    socket->writeDatagram(bytes, host, port);
}

bool ClientWorker::onLS(const QString& command)
{
    if(!assertNoParams("ls", command)) return false;
    if(!isAuthed) return false;
    QDirIterator it(currentDir);
    while (it.hasNext()) {
        it.next();
        it.fileInfo();
        // name size time isDir
        sendCommand(200, {it.fileName(), QString::number(it.fileInfo().size()), QString::number(it.fileInfo().lastModified().toSecsSinceEpoch()), it.fileInfo().isDir() ? "dir" : "file"}, true);
    }
    sendCommand(200, {});
    return true;
}

bool ClientWorker::onCD(const QString& command)
{
    if(!isAuthed) return false;
    auto list = getArguments("cd", command, 1);
    if(list.length() != 1) {
        sendCommand(400, {"usage:", "cd", "<path>"});
        return true;
    }
    auto pathBefore = currentDir;
    bool success = currentDir.cd(list.at(0));
    if(!success || !currentDir.path().startsWith(rootDir.path())) {
        currentDir = pathBefore;
        sendCommand(403, {"forbidden"});
        return true;
    }
    auto cwd = currentDir.path().mid(rootDir.path().length());
    sendCommand(200, {cwd.isEmpty() ? "/" : cwd});
    return true;
}

bool ClientWorker::onAUTH(const QString& command)
{
    auto list = getArguments("auth", command, 2);
    if(list.length() != 2) {
        sendCommand(400, {"usage:", "auth", "<name>", "<pass>"});
        return true;
    }
    if (list.at(0) == list.at(1)) {
        currentDir.setPath("D:/test/" + list.at(0));
        if(currentDir.exists()) {
            rootDir.setPath("D:/test/" + list.at(0));
            isAuthed = true;
            sendCommand(200, {"ok"});
        } else {
            sendCommand(403, {"no such user"});
        }
    } else {
        sendCommand(401, {"auth failed"});
    }
    return true;
}

bool ClientWorker::onMKDIR(const QString &command)
{
    if(!isAuthed) return false;
    auto list = getArguments("mkdir", command, 1);
    if(list.length() != 1) {
        sendCommand(400, {"usage:", "mkdir", "<name>"});
        return true;
    }

    if(currentDir.exists(list.at(0))) {
        sendCommand(422, {"file with same name already exists!"});
        return true;
    };

    bool success = currentDir.mkdir(list.at(0));
    if(success) {
        sendCommand(200, {"ok"});
    } else {
        sendCommand(500, {"failed to create dir"});
    }
    return true;
}

bool ClientWorker::onDELETE(const QString &command)
{
    if(!isAuthed) return false;
    auto list = getArguments("delete", command, 1);
    if(list.length() != 1) {
        sendCommand(400, {"usage:", "delete", "<name>"});
        return true;
    }

    if(!currentDir.exists(list.at(0))) {
        sendCommand(422, {"file with same name does not exist!"});
        return true;
    };

    bool success = currentDir.remove(list.at(0));
    if(success) {
        sendCommand(200, {"ok"});
    } else {
        sendCommand(500, {"failed to remove, maybe not empty dir"});
    }
    return true;
}

bool ClientWorker::onPUT(const QString& command)
{
    auto list = command.split(" ");
    if(list.length() == 3) {
        openedFile.setFileName(currentDir.absoluteFilePath(list.at(1)));
        qint64 size = list.at(2).toLongLong(&ok);
        if(ok) {
            totalLength = size - 1;
        }
        openedFile.open(QFile::WriteOnly);
        if(openedFile.isWritable()) {
            qDebug() << "? file opened for write.";
            waitingRawData = true;
            sendACKCommand("ok");
            return true;
        } else {
            qDebug() << "? failed to open file for write" << openedFile.fileName();
            emit(logging("? failed to open file for write: " + openedFile.fileName()));
        }
    }
    return false;
}

bool ClientWorker::readOneBlockToBuffer()
{
    QByteArray bytes = openedFile.read(readBlockSize);
    if(bytes.isEmpty() || bytes.isNull()) return false;
    buffer.push_back(bytes);
    if(bytes.length() != readBlockSize)
        return false;
    return true;
}

void ClientWorker::debugUnknownState(const QByteArray &data)
{
    qDebug() << "? received a datagram that should not be here"
             << host
             << port << data;
    emit(logging("? received a wrong datagram " + data));
    if(commandRetried == 2) {
        qDebug() << "? failed to get current state, close client.";
        emit(logging("! abort client."));
        rejectLastData();
        return;
    } else {
        commandRetried ++;
        retryLastCommand(false);
    }
}

void ClientWorker::retryLastCommand(bool retry)
{
    // Server don't need to retry
    Q_UNUSED(retry);
}

void ClientWorker::resetAllState()
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

bool ClientWorker::onGET(const QString& command)
{
    auto list = command.split(" ");
    if(list.length() == 2) {
        openedFile.setFileName(currentDir.absoluteFilePath(list.at(1)));
        openedFile.open(QFile::ReadOnly);
        if(openedFile.isReadable()) {
            qDebug() << "? file opened for read.";
            totalLength = openedFile.size() / readBlockSize + (openedFile.size() % readBlockSize != 0);
            for (int i = 0; i < 15; i++) {
                if(!readOneBlockToBuffer()) break;
            }
            isPendingReceiving = true;
            sendACKCommand(("size " + QString::number(totalLength)).toUtf8());
            return true;
        } else {
            qDebug() << "? failed to open file for read" << openedFile.fileName();
            emit(logging("? failed to open file for read: " + openedFile.fileName()));
        }
    }
    return false;
}

bool ClientWorker::onEXIT(const QString& command)
{
    Q_UNUSED(command);
    sendCommand(223, {"bye"});
    stopNow();
    return true;
}

bool ClientWorker::onHELP(const QString& command)
{
    if(!assertNoParams("help", command)) return false;
    sendCommand(200, {"HELP"});
    return true;
}

bool ClientWorker::onREADY(const QString &command)
{
    if(!assertNoParams("ready", command)) return false;
    if(!isPendingReceiving) return false;
    isPendingReceiving = false;
    writingRawData = true;
    onWriteRawData("ACK 0");
    return true;
}

bool ClientWorker::onNOSUCHMETHOD(const QString& command)
{
    Q_UNUSED(command);
    if (!isAuthed) {
        sendCommand(401, {"auth before you do"});
        return true;
    }
    sendCommand(500, {"no such method"});
    return true;
}

void ClientWorker::processCommand(const QByteArray* bytes)
{
    qDebug() << "processing command " << *bytes;
    // 不显示过长的指令
    if(bytes->length() < 50)
        emit(logging("> " + *bytes));

    bool handled = false;

    if(writingRawData) {
        onWriteRawData(*bytes);
        return;
    }
    if(waitingRawData) {
        onReceiveRawData(*bytes);
        return;
    }

    if (beforeParse != nullptr) {
        beforeParse(bytes);
    }
    if (handled) return;

    QString command(*bytes);
    command = command.trimmed();

    if (beforeHandle != nullptr) {
        beforeHandle(command);
    }
    if (handled) return;

    for (auto& i: commands) {
        if(command.toLower().startsWith(i.command)) {
            handled = i.callback(command);
            if(handled) break;
        }
    }

    if(!handled) {
        qDebug() << "? unhandled command" << command;
        emit(logging("? unhandled command " + command));
        onNOSUCHMETHOD(command);
    }
}

bool ClientWorker::assertNoParams(const QString& self, const QString &command)
{
    auto list = command.split(" ", QString::SkipEmptyParts);
    if(list.length() >= 2 || !self.startsWith(list.at(0)))
        return false;
    return true;
}

QStringList ClientWorker::getArguments(const QString &self, const QString &command, int max)
{
    auto list = command.split(" ", QString::SkipEmptyParts);
    if(list.length() > max + 1 || !self.startsWith(list.at(0))) {
        return {};
    }
    list.pop_front();
    return list;
}

void ClientWorker::onWriteRawData(const QByteArray &data)
{
    // 发送文件部分的核心机制
    // 一次将最多 15 个块读入缓冲区，并等待客户端发送 ACK，采用 Rollback-N 模式
    int begin = data.indexOf(" ");
    bool ok = false;
    int ack = data.mid(begin).toInt(&ok);
    qDebug() << "? got ack" << ack;
    emit(logging("? got ack" + QString::number(ack)));
    if(ack == totalLength) {
        writingRawData = false;
        qDebug() << "? sending data finished";
        emit(logging("? sending data finished"));
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
            emit(logging("? sending block " + QString::number(bufferBegin + i)));
            // 触发确认重传机制
            QThread::msleep(100);
            sendACKCommand(QByteArray::number(bufferBegin + i) + " " + buffer.at(i));
        }
        sendTimerId = startTimer(5000);
        return;
    }
    debugUnknownState(data);
}

void ClientWorker::onReceiveRawData(const QByteArray &data)
{
    // 接受文件部分的核心机制
    // 发送 ACK，采用 Rollback-N 模式
    int begin = data.indexOf(" ");
    bool ok = false;
    int block = data.left(begin).toInt(&ok);
    qDebug() << "? got block" << block;
    emit(logging("? got block " + QString::number(block)));
    if(ok && block == lastAck && block <= totalLength) {
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
            emit(logging("? file transfer finished"));
            resetAllState();
            sendACKCommand(QByteArray("ACK ") + QByteArray::number(block + 1));
        } else {
            expecting--;
            if(expecting == 0) {
                expecting = 15;
                sendACKCommand(QByteArray("ACK ") + QByteArray::number(lastAck));
            }
        }
    } else {
        if(dataTried == 2) {
            qDebug() << "? file transfer failed";
            emit(logging("? file transfer failed"));
            rejectLastData();
            return;
        }
        qDebug() << "? unknown block, expecting" << lastAck << "got" << block;
        emit(logging("? unknown block, expecting" + QString::number(lastAck) + " got " + QString::number(block)));
        expecting = 15;
        acceptLastData();
        dataTried ++;
        sendACKCommand(QByteArray("ACK ") + QByteArray::number(lastAck));
    }
}

void ClientWorker::acceptLastData()
{

}

void ClientWorker::rejectLastData()
{
    this->exit();
    stopNow();
}

