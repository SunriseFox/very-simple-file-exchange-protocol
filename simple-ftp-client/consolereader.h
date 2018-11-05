#ifndef CONSOLEREADER_H
#define CONSOLEREADER_H

#include <QTextStream>
#include <QString>
#include <QThread>
#include <QDebug>

class ConsoleReader : public QThread
{
    typedef std::function<void(QString)> Callback;
    Callback onCommandCallback;

    QString command;
    QTextStream cin;
    QTextStream cout;

public:
    ConsoleReader(Callback c);

protected:
    void run() override;
};

#endif // CONSOLEREADER_H
