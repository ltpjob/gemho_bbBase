#include "mainthread.h"
#include <QThread>
#include <QSettings>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include "crc32.h"
#include <QObject>
#include <QCoreApplication>
#include <QHostInfo>
#include <QNetworkInterface>


#define PORT 5768
#define HEAD 0xaa
#define ENUM 0x03

#pragma pack(1)
typedef struct tagupComDataHead
{
    quint8  start[2];
    qint16 type;
    qint32 size;
}upComDataHead;

typedef struct tagupComDataHead_udp
{
  upComDataHead hdt;
  uint32_t crc32;
}upComDataHead_udp;

typedef struct NetInfo_t
{
   uint8_t mac[6];  ///< Source Mac Address
   uint8_t ip[4];   ///< Source IP Address
   uint8_t sn[4];   ///< Subnet Mask
   uint8_t gw[4];   ///< Gateway IP Address
   uint8_t dns[4];  ///< DNS server IP Address
   uint8_t dhcp_mode;  ///< 1 - Static, 2 - DHCP
}NetInfo;

typedef struct tagdevInfo
{
  NetInfo netinfo;
  uint32_t cpuid[3];
  uint32_t crc32;
}devInfo;

typedef struct tagRetDevInfo
{
  upComDataHead_udp head;
  devInfo di;
}RetDevInfo;

#pragma pack()

static int bbBoxIpset(QString ip, QString mask, QString gw)
{
    QProcess process;
    QString strCmd;

    strCmd.sprintf("ifconfig eth0 %s netmask  %s", ip.toStdString().c_str(), mask.toStdString().c_str());
    process.start(strCmd);
    process.waitForFinished();

    strCmd.sprintf("route add default gw %s", gw.toStdString().c_str());
    process.start(strCmd);
    process.waitForFinished();

    strCmd.sprintf("ifconfig eth0 up");
    process.start(strCmd);
    process.waitForFinished();

    return 0;
}

static int bbBoxGetUID(quint32 *uid)
{
    uid[0] = 0;
    uid[1] = 0;
    uid[2] = 0;

    QFile CFG0("/sys/fsl_otp/HW_OCOTP_CFG0");
    if(CFG0.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QByteArray line = CFG0.readLine();
        QString str(line);
        uid[0] = str.toUInt(nullptr, 16);
    }

    QFile CFG1("/sys/fsl_otp/HW_OCOTP_CFG1");
    if(CFG1.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QByteArray line = CFG0.readLine();
        QString str(line);
        uid[1] = str.toUInt(nullptr, 16);
    }

    return 0;
}


mainThread::mainThread()
{
    m_udpSocket = new QUdpSocket(this);

    m_udpSocket->bind(PORT, QUdpSocket::ShareAddress);

    connect(m_udpSocket, SIGNAL(readyRead()), this, SLOT(processPendingDatagrams()));
}

void mainThread::processPendingDatagrams()
{
    while (m_udpSocket->hasPendingDatagrams())
    {
        QByteArray datagram;
        upComDataHead_udp *phead;

        datagram.resize(m_udpSocket->pendingDatagramSize());
        m_udpSocket->readDatagram(datagram.data(), datagram.size());

        if(datagram.size() < (int)(sizeof(upComDataHead_udp)))
            continue;

        phead = (upComDataHead_udp *)datagram.data();
        if(get_crc32(0, (uint8_t *)&phead->hdt, sizeof(phead->hdt)) == phead->crc32)
        {
            if(phead->hdt.start[0]==0x55&&phead->hdt.start[1]==HEAD
                    &&phead->hdt.size==(int)(datagram.size()-sizeof(*phead)))
            {
                if(phead->hdt.type==ENUM)
                {
                    qDebug("phead->hdt.type==ENUM");
                    quint32 uid[3];
                    bbBoxGetUID(uid);

                    QList<QNetworkInterface> nets = QNetworkInterface::allInterfaces();

                    int i = 0;
                    foreach(QNetworkInterface ni,nets)
                    {
                        i++;
                        qDebug()<<i<<ni.name()<<ni.hardwareAddress()<<ni.humanReadableName();
                    }


                }
            }
        }
    }
}

void mainThread::run()
{
    QString iniFileName = QCoreApplication::applicationDirPath() + "//bbBase_config.ini";

    QFileInfo fileInfo(iniFileName);
    QString defIp = "192.168.100.239";
    QString defMask = "255.255.255.0";
    QString defgw = "192.168.100.1";

    if(fileInfo.exists() == false)
    {
        QSettings configIniWrite(iniFileName, QSettings::IniFormat);

        configIniWrite.setValue("/netinfo/ip", "192.168.100.239");
        configIniWrite.setValue("/netinfo/mask", "255.255.255.0");
        configIniWrite.setValue("/netinfo/gw", "192.168.100.1");
    }

    QSettings configIniRead(iniFileName, QSettings::IniFormat);

    QString ip = configIniRead.value("/netinfo/ip").toString();
    QString mask = configIniRead.value("/netinfo/mask").toString();
    QString gw = configIniRead.value("/netinfo/gw").toString();

    QHostAddress test;
    if(!test.setAddress(ip) || !test.setAddress(mask) || !test.setAddress(gw))
    {
        ip = defIp;
        mask = defMask;
        gw = defgw;
    }

    bbBoxIpset(ip, mask, gw);

    while(1)
    {
        QCoreApplication::processEvents();
        usleep(100*1000);
    }
}
