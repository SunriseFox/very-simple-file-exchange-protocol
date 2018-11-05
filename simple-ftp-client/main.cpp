#include "responsehandler.h"
#include "consolereader.h"

#include <QCoreApplication>
#include <QThread>

static ResponseHandler* handler;

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    handler = new ResponseHandler();

    auto* thread = new ConsoleReader(std::bind(&ResponseHandler::onCommand, handler, std::placeholders::_1));

    a.connect(handler, &ResponseHandler::finished, thread, &QThread::quit);
    a.connect(thread, &QThread::finished, handler, &ResponseHandler::deleteLater);

    thread->start();

    return a.exec();
}
