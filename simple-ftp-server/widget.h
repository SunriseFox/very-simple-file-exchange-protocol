#ifndef WIDGET_H
#define WIDGET_H

#include "clientworker.h"

#include <QWidget>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QMap>
#include <QThread>

namespace Ui {
class Widget;
}

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void onNewDatagrams();

private:
    Ui::Widget *ui;
    QUdpSocket * socket;
    QMap<QString, ClientWorker*> knownClients;
};

#endif // WIDGET_H
