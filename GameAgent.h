#pragma once
#include "Package.h"
#include "TCPConnection.h"
#include "TCPServer.h"
#include "log.h"
class UdpDataSender: public DataProcessCallBack
{
public:
	UdpDataSender(SOCKET gameUdpSock, SOCKADDR_IN* pSrvAddr, int userid):
		sock(gameUdpSock), 
		srvAddr(pSrvAddr),
		id(userid),
		bIsHost(false)
	{
		buf = new char[UDP_SENDBUF_SIZE];
		memset(buf, 0, UDP_SENDBUF_SIZE);
		bufSize = 0;
	}
	void setIsHost(bool isHost)
	{
		bIsHost = isHost;
	}
	void OnDataProcess(Package* onePackage)
	{
		LogInfo("GameAgent: Use War3 Udp sock to  send tcp data to Server.");
		int len = onePackage->getSize();
		if (len > UDP_SENDBUF_SIZE - 4)
		{
			return;
		}
		if (bIsHost)
		{
			buf[FLAG_ISHOST_POS] = HOST;
		}
		else
		{
			buf[FLAG_ISHOST_POS] = CLIENT;
		}
		buf[CMDID_POS] = UC_DATA_SYNC;
		unsigned short tmpId = id;
		tmpId = htons(tmpId);
		memcpy(buf + USER_ID_MSB_POS, &tmpId, 2);
		memcpy(buf + UDP_HEADER_LENGTH, onePackage->getBuffer(), len);
		bufSize = UDP_HEADER_LENGTH + len;
		//因sendto被hook， 所以设置addrlen 为0， 在hook函数里判断特殊处理
		::sendto(sock, buf, bufSize, 0, (SOCKADDR*)srvAddr, 0);
	}
protected:
private:
	SOCKET			sock;
	SOCKADDR_IN*	srvAddr;
	int				id;
	char*			buf;
	int				bufSize;
	bool			bIsHost;

};
class GameAgent: public DataProcessCallBack, public juce::Thread
{
public:
	GameAgent(SOCKET gameUdpSock, SOCKADDR_IN* pSrvAddr, int userid):
		pool(nullptr), 
		srvAddr(pSrvAddr), 
		gameSock(gameUdpSock), 
		userId(userid),
		bIsActAsHost(false),
		clientTcp(nullptr),
		bIsAgentStop(false),
		bIsInit(false),
		Thread(""),
		serverTcp(nullptr),
		localChannelSock(INVALID_SOCKET)
	{
		srvDataSender = new UdpDataSender(gameUdpSock, pSrvAddr, userid);
		pool = new PackagePool<Package>(POOL_INCREASE_SIZE);
		queSendToGame.setRecyclePool(pool);
		queSendToSrv.setRecyclePool(pool);
		queSendToSrv.bindDataReceive(srvDataSender);
		GetLocalIP();
		buf = new char[UDP_SENDBUF_SIZE];
		memset(buf, 0, UDP_SENDBUF_SIZE);
		bufSize = 0;
	}
	String GetLocalIP()
	{
		// 获得本机主机名
		char hostname[MAX_PATH] = {0};
		gethostname(hostname,MAX_PATH);                
		struct hostent FAR* lpHostEnt = gethostbyname(hostname);
		if(lpHostEnt == NULL)
		{
			return SRV_TCP_IP;
		}

		// 取得IP地址列表中的第一个为返回的IP(因为一台主机可能会绑定多个IP)
		LPSTR lpAddr = lpHostEnt->h_addr_list[0];      

		// 将IP地址转化成字符串形式
		struct in_addr inAddr;
		memmove(&inAddr,lpAddr,4);
		m_strLocalIP = String( inet_ntoa(inAddr) );        

		return m_strLocalIP;
	}
	void init(bool tmpIsActAsHost)
	{
		LogInfo("GameAgent: init.");
		queSendToGame.bindDataReceive(this);
		queSendToGame.startThread();
		queSendToSrv.startThread();
		bIsActAsHost = tmpIsActAsHost;
		bIsInit = true;
		srvDataSender->setIsHost(!bIsActAsHost);
		if (bIsActAsHost)
		{
		}
		else
		{
			LogInfo("GameAgent: Create Tcp Client");
			clientTcp = new TCPConnection;
			int ret = clientTcp->tcpConnect(m_strLocalIP.toStdString(), WAR3_TCP_PORT);
			if (0 != ret)
			{
				LogError("GameAgent: Tcp connect Game failed.");
				bIsInit = false;
				return;
			}
			localChannelSock = clientTcp->getSockFd();			
		}
	}
	void OnDataProcess(Package* onePackage)
	{
		LogInfo("GameAgent: Use Tcp Sock to send data to Game.");
		if (INVALID_SOCKET != localChannelSock)
		{
			::send(localChannelSock, onePackage->getBuffer(), onePackage->getSize(), 0);
		}
		else
		{
			LogError("GameAgent: localChannelSock is INVALID.");
		}
	}
	void run()
	{
		LogInfo("GameAgent: Thread is Running.");
		if (!bIsInit)
		{
			LogWarn("GameAgent: it is not init.");
			return;
		}
		if (bIsActAsHost)
		{
			LogInfo("GameAgent: Create a TcpServer.");
			serverTcp = new TCPServer;
			serverTcp->tcpBind(WAR3_TCP_PORT);
			serverTcp->tcpListen();
			localChannelSock = serverTcp->tcpAccept();
		}
		while (!bIsAgentStop)
		{
			int ret = recv(localChannelSock, buf, UDP_SENDBUF_SIZE, 0);
			if (ret > UDP_SENDBUF_SIZE || ret < 0)
			{
				LogWarn("GameAgent: tcp receive func ret is INVALID.");
				continue;
			}
			Package* tempPack = pool->obtain();
			tempPack->fill(buf, ret);
			queSendToSrv.addPackage(tempPack);
		}
	}
	void addPackToGameQue(char* buf, int size)
	{
		LogInfo("GameAgent: add package to GameQue to send to Game.");
		Package* tempPackage = pool->obtain();
		tempPackage->fill(buf, size);
		queSendToGame.addPackage(tempPackage);
	}
protected:
private:
	PackagePool<Package> *	pool;
	PackageQue				queSendToGame;
	PackageQue				queSendToSrv;
	SOCKET					gameSock;
	SOCKADDR_IN*			srvAddr;
	int						userId;
	bool					bIsActAsHost;
	TCPConnection*			clientTcp;
	TCPServer*				serverTcp;
	SOCKET					localChannelSock;
	String					m_strLocalIP;
	bool					bIsAgentStop;
	bool					bIsInit;
	char*					buf;
	int						bufSize;
	UdpDataSender*			srvDataSender;
};