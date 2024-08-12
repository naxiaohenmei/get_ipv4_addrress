#include <WinSock2.h>
#include <Iphlpapi.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <locale>

std::string WString2String(const std::wstring& ws)
{
	std::string strLocale = setlocale(LC_ALL, "");
	const wchar_t* wchSrc = ws.c_str();
	size_t nDestSize = wcstombs(NULL, wchSrc, 0) + 1;
	char* chDest = new char[nDestSize];
	memset(chDest, 0, nDestSize);
	wcstombs(chDest, wchSrc, nDestSize);
	std::string strResult = chDest;
	delete[] chDest;
	setlocale(LC_ALL, strLocale.c_str());
	return strResult;
}

std::wstring String2WString(const std::string& s)
{
	std::string strLocale = setlocale(LC_ALL, "");
	const char* chSrc = s.c_str();
	size_t nDestSize = mbstowcs(NULL, chSrc, 0) + 1;
	wchar_t* wchDest = new wchar_t[nDestSize];
	wmemset(wchDest, 0, nDestSize);
	mbstowcs(wchDest, chSrc, nDestSize);
	std::wstring wstrResult = wchDest;
	delete[] wchDest;
	setlocale(LC_ALL, strLocale.c_str());
	return wstrResult;
}

void GetAdaptersAddressesInfo()
{
	 // 一次性申请超过15K的内存，不用多次分配，以提高效率，第一次调用GetAdaptersAddresses会提示内存不够
	 // 并把所需内存大小赋值给outBufLen，然后再次申请内存
	ULONG outBufLen = sizeof(IP_ADAPTER_ADDRESSES) * 403;
	
	PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
	if (GetAdaptersAddresses(AF_INET, 0, NULL, pAddresses, &outBufLen) == ERROR_BUFFER_OVERFLOW)
	{
		free(pAddresses);
		pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
	}

	if (GetAdaptersAddresses(AF_INET, 0, NULL, pAddresses, &outBufLen) == NO_ERROR)
	{
		PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
		while (pCurrAddresses != NULL)
		{
			std::wstring name = pCurrAddresses->FriendlyName;

			std::string str = WString2String(name);
			if (str == "eth0")
			{
				auto ptr = pCurrAddresses->FirstUnicastAddress;
				while (ptr)
				{
					if (ptr->Address.lpSockaddr->sa_family == AF_INET)
					{
						sockaddr_in* sa_in = (sockaddr_in*)ptr->Address.lpSockaddr;
						std::string ipv4Addr = inet_ntoa(sa_in->sin_addr);
						std::cout << "ipv4 " << ipv4Addr << std::endl;
					}
					else if (ptr->Address.lpSockaddr->sa_family == AF_INET6)
					{
						//sockaddr_in6 * sa_in6 = (sockaddr_in6*)ptr->Address.lpSockaddr;
						//char ipstr[INET6_ADDRSTRLEN];
						//inet_ntop(AF_INET6, &sa_in6->sin6_addr, ipstr, INET6_ADDRSTRLEN);
						//std::cout << "ipv6" << std::string(ipstr) << std::endl;
					}
					ptr = ptr->Next;
				}
			}

			pCurrAddresses = pCurrAddresses->Next;
		}
	}

	free(pAddresses);
}

int main()
{
	GetAdaptersAddressesInfo();

	getchar();

	return 0;
}
