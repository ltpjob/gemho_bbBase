#include <QCoreApplication>
#include "mainthread.h"









int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    mainThread mainth;
    mainth.start();

    return a.exec();
}
