#include "widget.h"
#include <QApplication>
#include <QThread>

int main(int argc, char *argv[])
{

    // main.cpp: 程序的入口点

    QApplication a(argc, argv);
    Widget w;
    w.show();

    return a.exec();
}
