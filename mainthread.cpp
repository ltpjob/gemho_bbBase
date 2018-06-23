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
#include <QtEndian>


#define PORT 5768
#define HEAD 0xaa
#define ENUM 0x03
#define GNSS_REPORTENUM 0x04
#define GNSS_SETNETINFO 0x05
#define RTK_STOP 0x06
#define RTK_START 0x07
#define RTK_RELOAD 0x08
#define REBOOT 0x09

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
    process.waitForFinished(1000);

    strCmd.sprintf("route add default gw %s", gw.toStdString().c_str());
    process.start(strCmd);
    process.waitForFinished(1000);

    strCmd.sprintf("route add 255.255.255.255 dev eth0");
    process.start(strCmd);
    process.waitForFinished(1000);

    strCmd.sprintf("ifconfig eth0 up");
    process.start(strCmd);
    process.waitForFinished(1000);

    return 0;
}

static int rtkStatusSet(QString status)
{
    QProcess process;
    QString strCmd;

    if(status == "reload")
    {
        strCmd.sprintf("supervisorctl reload gemho_rtk");
        process.start(strCmd);
        process.waitForFinished(5000);
    }
    else if(status == "start")
    {
        strCmd.sprintf("supervisorctl start gemho_rtk");
        process.start(strCmd);
        process.waitForFinished(3000);
    }
    else if(status == "stop")
    {
        strCmd.sprintf("supervisorctl stop gemho_rtk");
        process.start(strCmd);
        process.waitForFinished(3000);
    }
    else if(status == "reboot")
    {
        strCmd.sprintf("reboot");
        process.start(strCmd);
        process.waitForFinished(3000);
    }

    qDebug("%s", strCmd.toStdString().c_str());

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
        QByteArray line = CFG1.readLine();
        QString str(line);
        uid[1] = str.toUInt(nullptr, 16);
    }

    return 0;
}


mainThread::mainThread()
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
        QHostAddress hostAdd;
        quint16 fromPort;

        datagram.resize(m_udpSocket->pendingDatagramSize());
        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &hostAdd, &fromPort);

        if(datagram.size() < (int)(sizeof(upComDataHead_udp)))
            continue;

        phead = (upComDataHead_udp *)datagram.data();
        if(get_crc32(0, (uint8_t *)&phead->hdt, sizeof(phead->hdt)) == phead->crc32)
        {
            if(phead->hdt.start[0]==0x55&&phead->hdt.start[1]==HEAD
                    &&phead->hdt.size==(int)(datagram.size()-sizeof(*phead)))
            {
                if(phead->hdt.type == ENUM)
                {
                    RetDevInfo retdevi;
                    qDebug("phead->hdt.type == ENUM");

                    memset(&retdevi, 0, sizeof(retdevi));
                    bbBoxGetUID(retdevi.di.cpuid);

                    QString iniFileName = QCoreApplication::applicationDirPath() + "//bbBase_config.ini";
                    QFileInfo fileInfo(iniFileName);
                    if(fileInfo.exists() == true)
                    {
                        QSettings configIniRead(iniFileName, QSettings::IniFormat);
                        QString ip = configIniRead.value("/netinfo/ip").toString();
                        QString mask = configIniRead.value("/netinfo/mask").toString();
                        QString gw = configIniRead.value("/netinfo/gw").toString();
                        int tmp[4] = {};

                        sscanf(ip.toStdString().c_str(), "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
                        retdevi.di.netinfo.ip[0] = tmp[0];
                        retdevi.di.netinfo.ip[1] = tmp[1];
                        retdevi.di.netinfo.ip[2] = tmp[2];
                        retdevi.di.netinfo.ip[3] = tmp[3];

                        sscanf(mask.toStdString().c_str(), "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
                        retdevi.di.netinfo.sn[0] = tmp[0];
                        retdevi.di.netinfo.sn[1] = tmp[1];
                        retdevi.di.netinfo.sn[2] = tmp[2];
                        retdevi.di.netinfo.sn[3] = tmp[3];

                        sscanf(gw.toStdString().c_str(), "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
                        retdevi.di.netinfo.gw[0] = tmp[0];
                        retdevi.di.netinfo.gw[1] = tmp[1];
                        retdevi.di.netinfo.gw[2] = tmp[2];
                        retdevi.di.netinfo.gw[3] = tmp[3];
                    }
                    retdevi.di.crc32 = get_crc32(0, (uint8_t *)&retdevi.di, sizeof(retdevi.di)-sizeof(retdevi.di.crc32));

                    retdevi.head.hdt.start[0] = 0x55;
                    retdevi.head.hdt.start[1] = HEAD;
                    retdevi.head.hdt.type = GNSS_REPORTENUM;
                    retdevi.head.hdt.size = sizeof(devInfo);
                    retdevi.head.crc32 = get_crc32(0, (uint8_t *)&retdevi.head.hdt, sizeof(retdevi.head.hdt));

                    m_udpSocket->writeDatagram((char *)&retdevi, sizeof(retdevi), QHostAddress::Broadcast, fromPort);

                }
                else if(phead->hdt.type == GNSS_SETNETINFO)
                {
                    qDebug("phead->hdt.type == GNSS_SETNETINFO");
                    if(datagram.size() == sizeof(RetDevInfo))
                    {
                        RetDevInfo *prd = (RetDevInfo *)datagram.data();
                        if(prd->di.crc32 == get_crc32(0, (uint8_t *)&prd->di, sizeof(prd->di)-sizeof(prd->di.crc32)))
                        {
                            quint32 cpuid[3];
                            bbBoxGetUID(cpuid);
                            if(prd->di.cpuid[0] == cpuid[0] && prd->di.cpuid[1] == cpuid[1] && prd->di.cpuid[2] == cpuid[2])
                            {
                                QString ip;
                                QString mask;
                                QString gw;
                                QString iniFileName = QCoreApplication::applicationDirPath() + "//bbBase_config.ini";

                                ip.sprintf("%d.%d.%d.%d", prd->di.netinfo.ip[0], prd->di.netinfo.ip[1],
                                        prd->di.netinfo.ip[2], prd->di.netinfo.ip[3]);
                                mask.sprintf("%d.%d.%d.%d", prd->di.netinfo.sn[0], prd->di.netinfo.sn[1],
                                        prd->di.netinfo.sn[2], prd->di.netinfo.sn[3]);
                                gw.sprintf("%d.%d.%d.%d", prd->di.netinfo.gw[0], prd->di.netinfo.gw[1],
                                        prd->di.netinfo.gw[2], prd->di.netinfo.gw[3]);



                                QSettings configIniWrite(iniFileName, QSettings::IniFormat);

                                configIniWrite.setValue("/netinfo/ip", ip);
                                configIniWrite.setValue("/netinfo/mask", mask);
                                configIniWrite.setValue("/netinfo/gw", gw);

                                bbBoxIpset(ip, mask, gw);
                            }
                        }
                    }
                }
                else if(phead->hdt.type == RTK_RELOAD)
                {
                    qDebug("phead->hdt.type == RTK_RELOAD");
                    if(datagram.size() == sizeof(RetDevInfo))
                    {
                        RetDevInfo *prd = (RetDevInfo *)datagram.data();
                        if(prd->di.crc32 == get_crc32(0, (uint8_t *)&prd->di, sizeof(prd->di)-sizeof(prd->di.crc32)))
                        {
                            quint32 cpuid[3];
                            bbBoxGetUID(cpuid);
                            if(prd->di.cpuid[0] == cpuid[0] && prd->di.cpuid[1] == cpuid[1] && prd->di.cpuid[2] == cpuid[2])
                            {
                               rtkStatusSet("reload");
                            }
                        }
                    }
                }
                else if(phead->hdt.type == RTK_STOP)
                {
                    qDebug("phead->hdt.type == RTK_STOP");
                    if(datagram.size() == sizeof(RetDevInfo))
                    {
                        RetDevInfo *prd = (RetDevInfo *)datagram.data();
                        if(prd->di.crc32 == get_crc32(0, (uint8_t *)&prd->di, sizeof(prd->di)-sizeof(prd->di.crc32)))
                        {
                            quint32 cpuid[3];
                            bbBoxGetUID(cpuid);
                            if(prd->di.cpuid[0] == cpuid[0] && prd->di.cpuid[1] == cpuid[1] && prd->di.cpuid[2] == cpuid[2])
                            {
                               rtkStatusSet("stop");
                            }
                        }
                    }
                }
                else if(phead->hdt.type == RTK_START)
                {
                    qDebug("phead->hdt.type == RTK_START");
                    if(datagram.size() == sizeof(RetDevInfo))
                    {
                        RetDevInfo *prd = (RetDevInfo *)datagram.data();
                        if(prd->di.crc32 == get_crc32(0, (uint8_t *)&prd->di, sizeof(prd->di)-sizeof(prd->di.crc32)))
                        {
                            quint32 cpuid[3];
                            bbBoxGetUID(cpuid);
                            if(prd->di.cpuid[0] == cpuid[0] && prd->di.cpuid[1] == cpuid[1] && prd->di.cpuid[2] == cpuid[2])
                            {
                               rtkStatusSet("start");
                            }
                        }
                    }
                }
                else if(phead->hdt.type == REBOOT)
                {
                    qDebug("phead->hdt.type == REBOOT");
                    if(datagram.size() == sizeof(RetDevInfo))
                    {
                        RetDevInfo *prd = (RetDevInfo *)datagram.data();
                        if(prd->di.crc32 == get_crc32(0, (uint8_t *)&prd->di, sizeof(prd->di)-sizeof(prd->di.crc32)))
                        {
                            quint32 cpuid[3];
                            bbBoxGetUID(cpuid);
                            if(prd->di.cpuid[0] == cpuid[0] && prd->di.cpuid[1] == cpuid[1] && prd->di.cpuid[2] == cpuid[2])
                            {
                               rtkStatusSet("reboot");
                            }
                        }
                    }
                }
                else
                {
                    qDebug("undefine type!!");
                }

            }
        }
    }
}

void mainThread::run()
{
    while(1)
    {
        QCoreApplication::processEvents();
        usleep(100*1000);
    }
}
