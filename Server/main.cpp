#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <iostream>

using namespace std;

#pragma comment(lib, "WS2_32.lib")

#define DEFAULT_PORT "27015"
#define BUFFER_LENGTH 1460
#define MAX_CLIENTS		 5

VOID HandleClient(SOCKET client_socket);

int main()
{
	setlocale(LC_ALL, "");
	INT iResult = 0;
	DWORD dwLastError = 0;

	//0) Инициализация WS:
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		cout << "WSA init failed with: " << iResult << endl;
		return dwLastError;
	}

	//1) Инициализация переменных для Сокета:
	addrinfo* result = NULL;
	addrinfo* ptr = NULL;
	addrinfo hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	//2) Параметры Сокета:
	iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
	if (iResult != 0)
	{
		dwLastError = WSAGetLastError();
		cout << "getaddrinfo failed with error: " << dwLastError << endl;
		freeaddrinfo(result);
		WSACleanup();
		return dwLastError;
	}

	//3) Создание Сокета:
	SOCKET listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (listen_socket == INVALID_SOCKET)
	{
		dwLastError = WSAGetLastError();
		cout << "Socket creation failed with error: " << dwLastError << endl;
		freeaddrinfo(result);
		WSACleanup();
		return dwLastError;
	}

	//4) Bind socket:
	iResult = bind(listen_socket, result->ai_addr, result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		dwLastError = WSAGetLastError();
		cout << "Bind failed with error: " << dwLastError << endl;
		closesocket(listen_socket);
		freeaddrinfo(result);
		WSACleanup();
		return dwLastError;
	}

	//5) Запуск прослушивания сокета:
	iResult = listen(listen_socket, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		dwLastError = WSAGetLastError();
		cout << "Listen failed with error: " << dwLastError << endl;
		closesocket(listen_socket);
		freeaddrinfo(result);
		WSACleanup();
		return dwLastError;
	}

	//6) Обработка запроса от клиентов:
	INT n = 0;			//количество активных клиентов
	SOCKET client_sockets[MAX_CLIENTS] = {};
	DWORD threadIDs[MAX_CLIENTS] = {};
	HANDLE hThreads[MAX_CLIENTS] = {};
	cout << hThreads << endl;
	cout << HandleClient << endl;
	cout << "Accept client connections..." << endl;
	do
	{
		client_sockets[n] = accept(listen_socket, NULL, NULL);
		if (client_sockets[n] == INVALID_SOCKET)
		{
			dwLastError = WSAGetLastError();
			cout << "Accept failed with error: " << dwLastError << endl;
			closesocket(listen_socket);
			freeaddrinfo(result);
			WSACleanup();
			return dwLastError;
		}

		hThreads[n] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HandleClient,
			(LPVOID)client_sockets[n], 0, threadIDs + n);
		n++;
	} while (true);

	WaitForMultipleObjects(n, hThreads, TRUE, INFINITE);

	closesocket(listen_socket);
	freeaddrinfo(result);
	WSACleanup();
	return dwLastError;
}
VOID HandleClient(SOCKET client_socket)
{
	INT iResult = 0;
	DWORD dwLastError = 0;

	//7) Получение сообщения от клиента:
	sockaddr_in client_addr;
	int addr_len = sizeof(client_addr);
	getpeername(client_socket, (sockaddr*)&client_addr, &addr_len);

	char client_ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
	int client_port = ntohs(client_addr.sin_port);

	cout << "Client connected: " << client_ip << ":" << client_port << endl;

	do
	{
		CHAR send_buffer[BUFFER_LENGTH] = "Привет клиент";
		CHAR recv_buffer[BUFFER_LENGTH] = {};
		INT iSendResult = 0;

		iResult = recv(client_socket, recv_buffer, BUFFER_LENGTH, 0);
		if (iResult > 0)
		{
			cout << iResult << "Bytes received, Message: " << recv_buffer << endl;
			iSendResult = send(client_socket, recv_buffer, strlen(recv_buffer), 0);
			if (iSendResult == SOCKET_ERROR)
			{
				dwLastError = WSAGetLastError();
				cout << "Send failed with error: " << dwLastError << endl;
				break;
			}
			cout << "Bytes sent: " << iSendResult << endl;
		}
		else if (iResult == 0) cout << "Connection closing" << endl;
		else
		{
			dwLastError = WSAGetLastError();
			cout << "Receive failed with error: " << dwLastError << endl;
			break;
		}
	} while (iResult > 0);
	closesocket(client_socket);
}
