#pragma once
#include "UdpServer.h"
#include "MsgDef.h"

struct udpAddr 
{
	struct sockaddr* address;
	int addrLen;
	udpAddr():address(nullptr), addrLen(0)
	{
	}
	~udpAddr()
	{
		RELEASE(address)
		addrLen = 0;
	}
};
class UdpDataProcesser: public UdpDataCallBack
{
public:
	UdpDataProcesser()
	{
		memset(hostAddrHashArray, 0, sizeof(udpAddr*) * MAX_ID);
		memset(clientAddrHashArray, 0, sizeof(udpAddr*) * MAX_ID);
	}
	~UdpDataProcesser()
	{
		for (int i = 0; i < hostAddrs.size(); ++i)
		{
			delete hostAddrs.at(i);
		}
		hostAddrs.clear();
		for (int i = 0; i < clientAddrs.size(); ++i)
		{
			delete clientAddrs.at(i);
		}
		clientAddrs.clear();
	}
	void handleUdpData(SOCKET sock, char* buf, int bufLen, struct sockaddr* inUdpAddr, int addrLen)
	{
		unsigned short userId;
		memcpy(&userId, buf + USER_ID_MSB_POS, 2);
		userId = ntohs(userId);
		if (buf[FLAG_ISHOST_POS] == HOST)
		{
			if (nullptr != hostAddrHashArray[userId] && (UC_CREATE_MAP == buf[CMDID_POS] || UC_DATA_SYNC_UDP == buf[CMDID_POS]))
			{
				for (int i = 0; i < clientAddrs.size(); ++i)
				{
					::sendto(sock, 
						buf, bufLen, 0, 
						clientAddrs.at(i)->address, 
						clientAddrs.at(i)->addrLen);
				}
			}
			else if (nullptr != hostAddrHashArray[userId] && nullptr != clientAddrHashArray[userId])
			{
				::sendto(sock, 
					buf, bufLen, 0, 
					clientAddrHashArray[userId]->address, 
					clientAddrHashArray[userId]->addrLen);
			}
			else if (nullptr == hostAddrHashArray[userId])
			{
				udpAddr* structAddr = new udpAddr;
				structAddr->address = new struct sockaddr;
				memcpy(structAddr->address, inUdpAddr, addrLen);
				structAddr->addrLen = addrLen;
				hostAddrHashArray[userId] = structAddr;
				hostAddrs.push_back(structAddr);
			}
		}
		else if (buf[FLAG_ISHOST_POS] == CLIENT)
		{
			if (UC_DATA_SYNC_UDP == buf[CMDID_POS])
			{
				for (int i = 0; i < hostAddrs.size(); ++i)
				{
					::sendto(sock, 
						buf, bufLen, 0, 
						hostAddrs.at(i)->address, 
						hostAddrs.at(i)->addrLen);
					break;
				}
			}
			else if (nullptr != hostAddrHashArray[userId] && nullptr != clientAddrHashArray[userId])
			{
				::sendto(sock, 
					buf, bufLen, 0, 
					hostAddrHashArray[userId]->address, 
					hostAddrHashArray[userId]->addrLen);
			}
			else if (nullptr == clientAddrHashArray[userId])
			{
				udpAddr* structAddr = new udpAddr;
				structAddr->address = new struct sockaddr;
				memcpy(structAddr->address, inUdpAddr, addrLen);
				structAddr->addrLen = addrLen;
				clientAddrHashArray[userId] = structAddr;
				clientAddrs.push_back(structAddr);
			}

		}
	}
protected:
private:
	vector<udpAddr*> hostAddrs;
	vector<udpAddr*> clientAddrs;
	udpAddr* hostAddrHashArray[MAX_ID];
	udpAddr* clientAddrHashArray[MAX_ID];
};
class UdpServerManager
{
public:
	UdpServerManager():srv(nullptr), worker(nullptr)
	{
	}
	~UdpServerManager()
	{
		RELEASE(srv);
		RELEASE(worker);
	}
	void bindUdpDataProcesser(UdpServer* pSrv)
	{
		if (nullptr == pSrv)
		{
			return;
		}
		if (nullptr == worker)
		{
			worker = new UdpDataProcesser;
		}
		srv = pSrv;
		srv->setDataRecvCallBack(worker);
	}
protected:
private:
	UdpServer* srv;
	UdpDataProcesser* worker;
};