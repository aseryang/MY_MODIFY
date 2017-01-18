#include <WinSock2.h>
#include "../include/MinHook.h"
//#pragma comment(lib,"../lib/libMinHook.x86.lib")
#pragma comment(lib,"ws2_32.lib")
#include "MsgDef.h"
#include "SPIniReadWrite.h"
#include "GameAgent.h"
#include "log.h"
#include "UdpServer.h"

static String getModuleFilePathEx()  
{
	static String strModulePath;
	if (strModulePath.isEmpty())
	{
		wchar_t path[MAX_PATH];
		::GetModuleFileName(NULL, (LPWSTR)path, MAX_PATH);
		strModulePath = path;
		strModulePath = strModulePath.substring( 0, strModulePath.lastIndexOfChar(L'\\') + 1);
	}
	return strModulePath;
}

class ShareMem
{
private:
	ShareMem():clientCfgFilePath(File(getModuleFilePathEx()).getParentDirectory().getFullPathName()
		+ "/config/client_cfg.ini")
		, clientCfg(clientCfgFilePath)
	{
		//gIsLogON = clientCfg.getString("CONFIG", "islogOn").getIntValue() == 1;
		gIsLogON = true;
	}
public:
	static ShareMem* getInstance()
	{
		if (nullptr == Instance)
		{
			Instance = new ShareMem;
		}
		return Instance;
	}
	String getString(String strSetion, String strKey)
	{
		return clientCfg.getString(strSetion, strKey);
	}
	void setValue(String strSetion, String strKey, int nValue)
	{
		clientCfg.setValue(strSetion, strKey, nValue);
		clientCfg.writeFile(clientCfgFilePath);
	}
	void setValue(String strSetion, String strKey, String nValue)
	{
		clientCfg.setValue(strSetion, strKey, nValue);
		clientCfg.writeFile(clientCfgFilePath);
	}
protected:
private:
	static ShareMem* Instance;
	String clientCfgFilePath;
	CSPIniReadWrite clientCfg;	
};
class DataManager
{
private:
	DataManager(): singleInstanceCallBack(nullptr)
	{
		war3UdpSock = 0;
		udpPort		= ShareMem::getInstance()->getString("CONFIG", "roomUdpPort").getIntValue();
		srvIp		= ShareMem::getInstance()->getString("CONFIG", "address");
		gSendBuf	= new char[UDP_SENDBUF_SIZE];
		myid		= ShareMem::getInstance()->getString("CONFIG", "myId").getIntValue();
		roomid		= ShareMem::getInstance()->getString("CONFIG", "roomId").getIntValue();
		isHost = myid == roomid;
		ZeroMemory(&addrSrv, sizeof(SOCKADDR_IN));
		addrSrv.sin_addr.S_un.S_addr = inet_addr(srvIp.getCharPointer());        //set the host IP  
		addrSrv.sin_family = AF_INET;     //set the protocol family  
		addrSrv.sin_port = htons(udpPort);  
		ZeroMemory(&addrBroadCast, sizeof(SOCKADDR_IN));
		addrBroadCast.sin_addr.S_un.S_addr = INADDR_BROADCAST;        //set the host IP  
		addrBroadCast.sin_family = AF_INET;     //set the protocol family  
		addrBroadCast.sin_port = htons(WAR3_TCP_PORT);  
		bigEndianMyId = htons(myid);
		memset(hashAgentArray, 0, sizeof(GameAgent*)*MAX_ID);
		memset(hashUdpChannelArray, 0, sizeof(UdpServer*)*MAX_ID);
	}
public:
	static DataManager* getInstance()
	{
		if (nullptr == pInstance)
		{
			pInstance = new DataManager;
		}
		return pInstance;
	}
	void setUdpSock(SOCKET tempWar3UdpSock)
	{
		war3UdpSock = tempWar3UdpSock;
	}
	SOCKET getGameUdpSock()
	{
		return war3UdpSock;
	}
	int getUdpPort()
	{
		return udpPort;
	}
	int getMyId()
	{
		return myid;
	}
	bool getIsHost()
	{
		return isHost;
	}
	String& getIp()
	{
		return srvIp;
	}
	char* getBuff()
	{
		return gSendBuf;
	}
	SOCKADDR_IN& getAddrSrv()
	{
		return addrSrv;
	}
	SOCKADDR_IN& getBroadCastAddr()
	{
		return addrBroadCast;
	}
	unsigned short& getBigEndianMyid()
	{
		return bigEndianMyId;
	}
	class DataProcesser : public UdpDataCallBack
	{
	public:
		DataProcesser(){}
		~DataProcesser(){}
		virtual void handleUdpData(SOCKET sock, char* buf, int bufLen, struct sockaddr* inUdpAddr, int addrLen)
		{
			if (buf[CMDID_POS] == UC_JOIN_MAP_REQ)
			{
				LogInfo("handleUdpData: Receive Join map request.");
				unsigned short tempId = 0;
				memcpy(&tempId, buf + USER_ID_MSB_POS, 2);
				tempId = ntohs(tempId);
				DataManager::getInstance()->createGameAgent(tempId);
			}
			else if (buf[CMDID_POS] == UC_DATA_SYNC)
			{
				LogInfo("handleUdpData: TCP data sync.");
				unsigned short tempId = 0;
				memcpy(&tempId, buf + USER_ID_MSB_POS, 2);
				tempId = ntohs(tempId);
				DataManager::getInstance()->addSyncData(tempId, buf + UDP_HEADER_LENGTH, bufLen - UDP_HEADER_LENGTH);
			}
			else if (buf[CMDID_POS] == UC_DATA_SYNC_UDP || buf[CMDID_POS] == UC_CREATE_MAP)
			{
				LogInfo("handleUdpData: UDP msg broad cast.");
				//broadcast
				::sendto(sock, buf + UDP_HEADER_LENGTH, bufLen - UDP_HEADER_LENGTH, 0, nullptr, addrLen);
			}
		}
	protected:
	private:
	};
public:
	void createGameAgent(int userid, bool isActAsHost = false)
	{
		LogInfo("Datamanager: create a GameAgent.");
		if (hashAgentArray[userid] != nullptr)
		{
			LogWarn("Datamanager: user GameAgent is exist.");
			return;
		}
		GameAgent* pAgent = new GameAgent(war3UdpSock, &addrSrv, userid);
		pAgent->init(isActAsHost);
		pAgent->startThread();
		hashAgentArray[userid] = pAgent;
	}
	void createUdpChannel()
	{
		LogInfo("createUdpChannel");
		if (hashUdpChannelArray[myid] != nullptr)
		{
			return;
		}
		UdpServer* udpChannel = new UdpServer;
		singleInstanceCallBack = new DataProcesser;
		if (!udpChannel->init())
		{
			LogInfo("createUdpChannel: udpserver init succeed.");
			udpChannel->udpBind(udpPort);
			war3UdpSock = udpChannel->getSock();
			udpChannel->setDataRecvCallBack(singleInstanceCallBack);
			udpChannel->startThread();
			hashUdpChannelArray[myid] = udpChannel;
		}
		else
		{
			LogError("createUdpChannel: udpserver init error.");
		}
	}
	void addSyncData(int userid, char* buf, int size)
	{
		LogInfo("Datamanager: add sync data to gameque.");
		if (hashAgentArray[userid] != nullptr)
		{
			hashAgentArray[userid]->addPackToGameQue(buf, size);
		}
	}
private:
	static DataManager* pInstance;

	SOCKET  war3UdpSock;
	int		udpPort;	
	String	srvIp;
	bool	isHost;
	char *	gSendBuf;
	int		myid;
	int     roomid;
	SOCKADDR_IN addrSrv;
	SOCKADDR_IN	addrBroadCast;
	unsigned short bigEndianMyId;
	GameAgent* hashAgentArray[MAX_ID];
	UdpServer* hashUdpChannelArray[MAX_ID];
	DataProcesser*		singleInstanceCallBack;
};
