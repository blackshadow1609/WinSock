#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <iostream>
#include <vector>
#include <string>

using namespace std;

#pragma comment(lib, "WS2_32.lib")

#define DEFAULT_PORT "27015"
#define BUFFER_LENGTH 1460
#define MAX_CLIENTS		 3
#define g_sz_SORRY "Error: Количество подключений превышено"
#define IP_STR_MAX_LENGTH 16

// Глобальные переменные
INT n = 0;			//количество активных клиентов
SOCKET client_sockets[MAX_CLIENTS] = {};
DWORD threadIDs[MAX_CLIENTS] = {};
HANDLE hThreads[MAX_CLIENTS] = {};
CRITICAL_SECTION cs; // Критическая секция для синхронизации

VOID WINAPI HandleClient(LPVOID lpParam);
VOID BroadcastMessage(const CHAR* message, SOCKET sender_socket);
VOID PrintClientsInfo();

int main()
{
	setlocale(LC_ALL, "");
	DWORD dwLastError = 0;
	INT iResult = 0;

	// Инициализация критической секции
	InitializeCriticalSection(&cs);

	//0) Инициализация WS:
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		cout << "WSA init failed with: " << iResult << endl;
		DeleteCriticalSection(&cs);
		return iResult;
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
		cout << "getaddrinfo failed with error: " << iResult << endl;
		DeleteCriticalSection(&cs);
		WSACleanup();
		return iResult;
	}

	//3) Создание Сокета:
	SOCKET listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (listen_socket == INVALID_SOCKET)
	{
		dwLastError = WSAGetLastError();
		cout << "Socket creation failed with error: " << dwLastError << endl;
		freeaddrinfo(result);
		DeleteCriticalSection(&cs);
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
		DeleteCriticalSection(&cs);
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
		DeleteCriticalSection(&cs);
		WSACleanup();
		return dwLastError;
	}

	// Инициализация массива сокетов
	for (int i = 0; i < MAX_CLIENTS; i++) {
		client_sockets[i] = INVALID_SOCKET;
		hThreads[i] = NULL;
		threadIDs[i] = 0;
	}

	cout << "Сервер запущен. Ожидание подключений..." << endl;
	cout << "Максимальное количество клиентов: " << MAX_CLIENTS << endl;
	PrintClientsInfo();

	//6) Обработка запроса от клиентов:
	do
	{
		SOCKET client_socket = accept(listen_socket, NULL, NULL);
		if (client_socket == INVALID_SOCKET)
		{
			dwLastError = WSAGetLastError();
			cout << "Accept failed with error: " << dwLastError << endl;
			continue;
		}

		EnterCriticalSection(&cs);

		if (n < MAX_CLIENTS)
		{
			// Поиск свободного слота
			int freeSlot = -1;
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (client_sockets[i] == INVALID_SOCKET || client_sockets[i] == 0) {
					freeSlot = i;
					break;
				}
			}

			if (freeSlot != -1) {
				client_sockets[freeSlot] = client_socket;
				hThreads[freeSlot] = CreateThread(NULL, 0,
					(LPTHREAD_START_ROUTINE)HandleClient,
					(LPVOID)client_socket, 0, &threadIDs[freeSlot]);

				if (hThreads[freeSlot] == NULL) {
					cout << "CreateThread failed: " << GetLastError() << endl;
					closesocket(client_socket);
					client_sockets[freeSlot] = INVALID_SOCKET;
				}
				else {
					n++;
					cout << "\nНовый клиент подключен!" << endl;
					PrintClientsInfo();

					// Отправляем приветственное сообщение новому клиенту
					string welcomeMsg = "Добро пожаловать на сервер! Активных клиентов: " +
						to_string(n) + ", свободных слотов: " +
						to_string(MAX_CLIENTS - n);
					send(client_socket, welcomeMsg.c_str(), welcomeMsg.length(), 0);
				}
			}
		}
		else
		{
			cout << "Отклонено подключение: достигнут лимит клиентов" << endl;
			send(client_socket, g_sz_SORRY, strlen(g_sz_SORRY), 0);
			closesocket(client_socket);
		}

		LeaveCriticalSection(&cs);
	} while (true);

	// Ожидание завершения всех потоков
	WaitForMultipleObjects(MAX_CLIENTS, hThreads, TRUE, INFINITE);

	// Закрытие дескрипторов потоков
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (hThreads[i] != NULL) {
			CloseHandle(hThreads[i]);
		}
	}

	closesocket(listen_socket);
	freeaddrinfo(result);
	DeleteCriticalSection(&cs);
	WSACleanup();
	return 0;
}

INT GetSlotIndex(DWORD dwID)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (threadIDs[i] == dwID) return i;
	}
	return -1;
}

VOID Shift(INT start)
{
	if (start < 0 || start >= MAX_CLIENTS) return;

	for (INT i = start; i < MAX_CLIENTS - 1; i++)
	{
		client_sockets[i] = client_sockets[i + 1];
		threadIDs[i] = threadIDs[i + 1];
		hThreads[i] = hThreads[i + 1];
	}
	client_sockets[MAX_CLIENTS - 1] = INVALID_SOCKET;
	threadIDs[MAX_CLIENTS - 1] = 0;
	hThreads[MAX_CLIENTS - 1] = NULL;
	n--;
}

VOID BroadcastMessage(const CHAR* message, SOCKET sender_socket)
{
	EnterCriticalSection(&cs);

	string broadcastMsg = message;

	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (client_sockets[i] != INVALID_SOCKET && client_sockets[i] != 0 &&
			client_sockets[i] != sender_socket) {
			send(client_sockets[i], broadcastMsg.c_str(), broadcastMsg.length(), 0);
		}
	}

	LeaveCriticalSection(&cs);
}

VOID PrintClientsInfo()
{
	EnterCriticalSection(&cs);

	cout << "\n=== Статус сервера ===" << endl;
	cout << "Активных клиентов: " << n << endl;
	cout << "Свободных слотов: " << (MAX_CLIENTS - n) << endl;
	cout << "Список клиентов:" << endl;

	bool hasClients = false;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (client_sockets[i] != INVALID_SOCKET && client_sockets[i] != 0) {
			cout << "  Клиент #" << (i + 1) << " (ID потока: " << threadIDs[i] << ")" << endl;
			hasClients = true;
		}
	}

	if (!hasClients) {
		cout << "  Нет активных клиентов" << endl;
	}

	cout << "====================\n" << endl;

	LeaveCriticalSection(&cs);
}

VOID WINAPI HandleClient(LPVOID lpParam)
{
	SOCKET client_socket = (SOCKET)lpParam;

	SOCKADDR_IN peer{};
	CHAR address[IP_STR_MAX_LENGTH] = {};
	INT address_length = sizeof(peer);
	getpeername(client_socket, (SOCKADDR*)&peer, &address_length);
	inet_ntop(AF_INET, &peer.sin_addr, address, IP_STR_MAX_LENGTH);
	INT port = ntohs(peer.sin_port);

	cout << "Клиент подключился: " << address << ":" << port << endl;

	INT iResult = 0;
	DWORD dwLastError = 0;
	CHAR recv_buffer[BUFFER_LENGTH] = {};

	do
	{
		ZeroMemory(recv_buffer, BUFFER_LENGTH);
		iResult = recv(client_socket, recv_buffer, BUFFER_LENGTH, 0);

		if (iResult > 0)
		{
			string fullMessage = string(address) + ":" + to_string(port) + " говорит: " + recv_buffer;

			cout << fullMessage << endl;

			send(client_socket, recv_buffer, strlen(recv_buffer), 0);

			if (strstr(recv_buffer, "quit") == NULL && strstr(recv_buffer, "exit") == NULL) {
				BroadcastMessage(fullMessage.c_str(), client_socket);
			}

			PrintClientsInfo();
		}
		else if (iResult == 0) {
			cout << "Клиент " << address << ":" << port << " отключился" << endl;
		}
		else
		{
			dwLastError = WSAGetLastError();
			cout << "Receive failed with error: " << dwLastError << endl;
			break;
		}
	} while (iResult > 0 && strstr(recv_buffer, "quit") == NULL && strstr(recv_buffer, "exit") == NULL);

	EnterCriticalSection(&cs);

	DWORD dwID = GetCurrentThreadId();
	INT slotIndex = GetSlotIndex(dwID);
	if (slotIndex != -1) {
		Shift(slotIndex);
	}

	cout << "Клиент " << address << ":" << port << " отключился" << endl;
	PrintClientsInfo();

	LeaveCriticalSection(&cs);

	closesocket(client_socket);
	ExitThread(0);
}