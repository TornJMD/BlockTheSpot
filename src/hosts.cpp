#include "stdafx.h"

bool g_UseAdGuard = true;
bool g_Log = false;
bool g_Skip_wpad = false;
bool g_WinHttpReadDataFix = false;

std::ofstream Log_DNS;
std::ofstream Log_GetAddr;
std::ofstream Log_WinHttp;
extern PIP4_ARRAY pSrvList;

std::vector<std::string> blacklist;
std::vector<std::string> whitelist;

// support.microsoft.com/en-us/help/831226/
// how-to-use-the-dnsquery-function-to-resolve-host-names-and-host-addres
// blogs.msdn.microsoft.com/winsdk/2014/12/17/
// dnsquery-sample-to-loop-through-multiple-ip-addresses/

bool adguard_dnsblock (const char* nodename) {
	DNS_STATUS dnsStatus;
	PDNS_RECORD QueryResult;
	bool isBlock = false;
	static int fail_count = 0;
	if (!g_UseAdGuard) return false;
	
	if (fail_count > 5) {
		if (g_Log) {
			Log_DNS << "AdGuard DNS lookup disable! fail resolve > 5 times" << '\n';
		}
		g_UseAdGuard = false;
		return false;
	}

	for (auto block : blacklist) {
		if (0 == _stricmp (block.c_str (), nodename))
			return true;
	}
	for (auto allow : whitelist) {
		if (0 == _stricmp (allow.c_str (), nodename))
			return false;
	}

	dnsStatus = DnsQuery (nodename,
						  DNS_TYPE_A,
						  DNS_QUERY_WIRE_ONLY,
						  pSrvList,
						  &QueryResult,
						  NULL); // Reserved

	if (0 == dnsStatus) {
		if (QueryResult) {
			for (auto p = QueryResult; p; p = p->pNext) {
				if (0 == p->Data.A.IpAddress) {
					isBlock = true; // AdGuard Block
					blacklist.push_back (nodename); // add to blacklist
					break;	// no more processing
				}
			}
			DnsRecordListFree (QueryResult, DnsFreeRecordList);

			if (!isBlock)
				whitelist.push_back (nodename); // add to whitelist
		} // QueryResult
	} else { // dnsStatus
		fail_count++;
	}
	if (g_Log && isBlock) {
		Log_DNS << nodename << " blocked" << '\n';
	}

	return isBlock;
}

int WINAPI getaddrinfohook (DWORD RetAddr,
							pfngetaddrinfo fngetaddrinfo,
							const char* nodename,
							const char* servname,
							const struct addrinfo* hints,
							struct addrinfo** res)
{

	auto result = fngetaddrinfo (nodename,
								 servname,
								 hints,
								 res);
	if (0 == result) { // GetAddrInfo return 0 on success
		if (NULL != strstr (nodename, "google"))
			return WSANO_RECOVERY;

		// Web Proxy Auto-Discovery (WPAD)
		if (0 == _stricmp (nodename, "wpad"))
			return g_Skip_wpad ? WSANO_RECOVERY : result;

		// AdGuard DNS
		if (adguard_dnsblock (nodename))
			return WSANO_RECOVERY;

		if (g_Log) {
			Log_GetAddr << nodename << '\n';
		}
	}
	return result;
}

// withthis you can replace other json response as well
int WINAPI winhttpreaddatahook (DWORD RetAddr,
								pfnwinhttpreaddata fnwinhttpreaddata,
								HINTERNET hRequest,
								LPVOID lpBuffer,
								DWORD dwNumberOfBytesToRead,
								LPDWORD lpdwNumberOfBytesRead)
{
	if (!fnwinhttpreaddata (hRequest,
							lpBuffer,
							dwNumberOfBytesToRead,
							lpdwNumberOfBytesRead)) {
		return false;
	}

	char* pdest = strstr ((LPSTR)lpBuffer, "{\"login_url");
	if (pdest != NULL) {
		return true;
	}

	pdest = strstr ((LPSTR)lpBuffer, "{\"credentials");
	if (pdest != NULL) {
		return true;
	}
	if (g_Log) {
		std::string data ((char*)lpBuffer, dwNumberOfBytesToRead);
		Log_WinHttp << data << '\n';
	}
	if (g_WinHttpReadDataFix) return false;

	SecureZeroMemory (lpBuffer, dwNumberOfBytesToRead);
	return true;
}

