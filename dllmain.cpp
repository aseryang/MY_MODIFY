
#include "dllmain.h"
DataManager* DataManager::pInstance = nullptr;
ShareMem* ShareMem::Instance = nullptr;
bool gIsGetGameUdpSock = false;

typedef int (WINAPI *MESSAGEBOXW)(HWND, LPCWSTR, LPCWSTR, UINT);
typedef int (PASCAL  *RECVFROM)( SOCKET s, char * buf, int len, int flags, struct sockaddr * from, int * fromlen);
typedef int (PASCAL  *SENDTO)( SOCKET s, const char * buf, int len, int flags, const struct sockaddr * to, int tolen);
typedef int (*CONNECT)(SOCKET s, const struct sockaddr  * name, int namelen);
MESSAGEBOXW fpMessageBoxW	= NULL;
RECVFROM	fpRecvfrom		= NULL;
SENDTO		fpSendto		= NULL;
CONNECT		fpConnect		= NULL;

int WINAPI DetourMessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
	return fpMessageBoxW(hWnd, L"Hooked!", lpCaption, uType);
}
int PASCAL DetourRecvfrom( SOCKET s, char * buf, int len, int flags, struct sockaddr * from, int * fromlen)
{
	MessageBoxW(NULL, L"DetourRecvfrom called", L"MH", MB_OK);
	LogInfo("DetourRecvfrom: Udp recvfrom function called.");
	int ret = fpRecvfrom(s, buf, len, flags, from, fromlen);
	if (ret < UDP_HEADER_LENGTH)
	{
		LogError("Udp received data length less than 4.");
		return ret;
	}
	if (buf[CMDID_POS] == UC_JOIN_MAP_REQ)
	{
		LogInfo("Receive Join map request.");
		unsigned short tempId = 0;
		memcpy(&tempId, buf + USER_ID_MSB_POS, 2);
		tempId = ntohs(tempId);
		DataManager::getInstance()->createGameAgent(tempId);
		return ret;
	}
	else if (buf[CMDID_POS] == UC_DATA_SYNC)
	{
		LogInfo("Udp data sync.");
		unsigned short tempId = 0;
		memcpy(&tempId, buf + USER_ID_MSB_POS, 2);
		tempId = ntohs(tempId);
		DataManager::getInstance()->addSyncData(tempId, buf + UDP_HEADER_LENGTH, ret - UDP_HEADER_LENGTH);
	}
	memmove(buf, buf + UDP_HEADER_LENGTH, ret - UDP_HEADER_LENGTH);
	return ret - 4;
}
int PASCAL DetourSendto( SOCKET s, const char * buf, int len, int flags, const struct sockaddr * to, int tolen)
{
	LogInfo("DetourSendto: Udp sendto called.");
	if (nullptr == to)
	{
		//my broadcast udp msg
		int ret = fpSendto(s, buf, len, 0, (SOCKADDR*) &(DataManager::getInstance()->getBroadCastAddr()), sizeof(SOCKADDR));  
		return ret;
	}
	if (tolen == 0)
	{
		//UC_DATA_SYNC 消息
		int ret = fpSendto(s, buf, len, 0, to, sizeof(SOCKADDR));  
		return ret;
	}
	if (!gIsGetGameUdpSock)
	{
		gIsGetGameUdpSock = true;
		DataManager::getInstance()->createUdpChannel();
	}

	if (buf[WAR3_HEADER_POS] != WARCRAFT || len + 4 > UDP_SENDBUF_SIZE)
	{
		return 0;
	}
	char* gSendBuf = DataManager::getInstance()->getBuff();
	memcpy(gSendBuf + USER_ID_MSB_POS, &(DataManager::getInstance()->getBigEndianMyid()), 2);
	memcpy(gSendBuf + UDP_HEADER_LENGTH, buf, len);
	if (DataManager::getInstance()->getIsHost())
	{
		gSendBuf[FLAG_ISHOST_POS] = HOST;
		if (buf[WAR3_UDP_CMDID_POS] == BATTLE_CREATE)
		{
			gSendBuf[CMDID_POS]       = UC_CREATE_MAP;
		}
		else
		{
			gSendBuf[CMDID_POS]       = UC_DATA_SYNC_UDP;
		}
	}
	else
	{
		gSendBuf[FLAG_ISHOST_POS] = CLIENT;
	}

	int ret = fpSendto(DataManager::getInstance()->getGameUdpSock(), gSendBuf, len + 4, 0, (SOCKADDR*) &(DataManager::getInstance()->getAddrSrv()), sizeof(SOCKADDR));  
	return ret;
}
int  DetourConnect(SOCKET s, const struct sockaddr  * name, int namelen)
{
	//MessageBoxW(NULL, L"connect hooked...", L"MH", MB_OK);
	LogInfo("DetourConnect: Tcp connect Function called.");
	int ret = 0;
	if (!(DataManager::getInstance()->getIsHost()))
	{
		char* gSendBuf = DataManager::getInstance()->getBuff();
		gSendBuf[CMDID_POS]       = UC_JOIN_MAP_REQ;
		gSendBuf[FLAG_ISHOST_POS] = CLIENT;
		memcpy(gSendBuf + USER_ID_MSB_POS, &(DataManager::getInstance()->getBigEndianMyid()), 2);
		fpSendto(DataManager::getInstance()->getGameUdpSock(), gSendBuf, UDP_HEADER_LENGTH, 0, (SOCKADDR*) &(DataManager::getInstance()->getAddrSrv()), sizeof(SOCKADDR));  


		DataManager::getInstance()->createGameAgent(DataManager::getInstance()->getMyId(), true);

		SOCKADDR_IN Sersock;//用于服务器的监听SOCKET
		ZeroMemory(&Sersock,sizeof(Sersock)); 
		Sersock.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//所有可用IP
		Sersock.sin_family = AF_INET; 
		Sersock.sin_port = htons(WAR3_TCP_PORT);//端口号 
		ret = fpConnect(s, (const struct sockaddr *)&Sersock, sizeof(Sersock));
		int times = 1;
		const int maxTryConnectTimes = 3;
		while (ret != 0 && times <= 3)
		{
			LogError("Tcp connect GameAgent Failed, try one more time.");
			Sleep(50);
			ret = fpConnect(s, (const struct sockaddr *)&Sersock, sizeof(Sersock));
		}
	}
	return ret;
}
int doHook()
{
	// Initialize MinHook.
	if (MH_Initialize() != MH_OK)
	{
		return 1;
	}
	// Create a hook for MessageBoxW, in disabled state.
// 	if (MH_CreateHook(&recvfrom, &DetourRecvfrom, (LPVOID*)&fpRecvfrom) != MH_OK)
// 	{
// 		MessageBoxW(NULL, L"hook recvfrom failed...", L"MH", MB_OK);
// 		return 1;
// 	}
	if (MH_CreateHook(&sendto, &DetourSendto, (LPVOID*)&fpSendto) != MH_OK)
	{
		MessageBoxW(NULL, L"hook sendto failed...", L"MH", MB_OK);
		return 1;
	}
	if (MH_CreateHook(&connect, &DetourConnect, (LPVOID*)&fpConnect) != MH_OK)
	{
		MessageBoxW(NULL, L"hook connect failed...", L"MH", MB_OK);
		return 1;
	}

	// Enable the hook for MessageBoxW.
// 	if (MH_EnableHook(&recvfrom) != MH_OK)
// 	{
// 		return 1;
// 	}
	if (MH_EnableHook(&sendto) != MH_OK)
	{
		return 1;
	}
	if (MH_EnableHook(&connect) != MH_OK)
	{
		return 1;
	}

	return 0;
}
BOOL WINAPI DllMain( HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
	)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		{
			int ret = doHook();
			gIsLogON = true;
			LogInfo("DllMain: dll attach.");
			if (ret)
			{
				MessageBoxW(NULL, L"hook failed...", L"MH", MB_OK);
			}
			else
			{
				LogInfo("DllMain: hook succeed.");
				MessageBoxW(NULL, L"hook succeed...", L"MH", MB_OK);
			}
		}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
