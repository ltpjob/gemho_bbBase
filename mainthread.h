#ifndef MAINTHREAD_H
#define MAINTHREAD_H

#include <QThread>
#include <QUdpSocket>

class mainThread : public QThread
{
    Q_OBJECT
public:
    mainThread();
    void run(); //声明继承于QThread虚函数 run()

private:
    QUdpSocket *m_udpSocket;

private slots:
    void processPendingDatagrams();
};

#endif // MAINTHREAD_H
