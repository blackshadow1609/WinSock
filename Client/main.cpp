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

int main()
{
	setlocale(LC_ALL, "");

	cout << "Hello WinSock!" << endl;

	INT iResult = 0;
	DWORD dwLastError = 0;

	//0) Инициализация:
	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		cout << "WSAStartup failed: " << iResult << endl;
		return iResult;
	}

	//1) Переменные для хранения информации о Сокете:
	addrinfo* result = NULL;
	addrinfo* ptr = NULL;
	addrinfo hints = { 0 };
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	//2) Информация о сервере к которому будет подключение:
	iResult = getaddrinfo("192.168.1.100", DEFAULT_PORT, &hints, &result);
	if (iResult != 0)
	{
		cout << "getaddrinfo failed: " << iResult << endl;
		WSACleanup();
		return iResult;
	}

	//3) Создание Сокета для подключения к серверу:
	ptr = result;
	SOCKET connect_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
	if (connect_socket == INVALID_SOCKET)
	{
		dwLastError = WSAGetLastError();
		cout << "Socket error: " << dwLastError << endl;
		freeaddrinfo(result);
		WSACleanup();
		return dwLastError;
	}

	//4) Подключение к серверу:
	iResult = connect(connect_socket, ptr->ai_addr, (INT)ptr->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		dwLastError = WSAGetLastError();
		cout << "Connection error: " << dwLastError << endl;
		closesocket(connect_socket);
		freeaddrinfo(result);
		WSACleanup();
		return dwLastError;
	}

	//5) Отправка данных на сервер:
	CHAR send_buffer[BUFFER_LENGTH] = "Hello Server, I am client";
	do
	{
		iResult = send(connect_socket, send_buffer, strlen(send_buffer), 0);
		if (iResult == SOCKET_ERROR)
		{
			dwLastError = WSAGetLastError();
			cout << "Send failed with error: " << dwLastError << endl;
			closesocket(connect_socket);
			freeaddrinfo(result);
			WSACleanup();
			return dwLastError;
		}
		cout << iResult << " Bytes sent" << endl;

		//6) Ожидание ответа от сервера:
		CHAR recv_buffer[BUFFER_LENGTH] = {};
		//do
		{
			iResult = recv(connect_socket, recv_buffer, BUFFER_LENGTH, 0);
			if (iResult > 0) cout << iResult << " Bytes received, Message:\t" << recv_buffer << ".\n";
			else if (iResult == 0) cout << "Connection closed" << endl;
			else cout << "Receive failed with error: " << WSAGetLastError() << endl;
		} //while (iResult > 0);

		ZeroMemory(send_buffer, BUFFER_LENGTH);
		cout << "Введите сообщение: ";
		SetConsoleCP(1251);
		cin.getline(send_buffer, BUFFER_LENGTH);
		SetConsoleCP(866);
	} while (strstr(send_buffer, "exit") == 0 && strstr(send_buffer, "quit") == 0);
	//} while (strcmp(send_buffer, "exit") != 0 && strcmp(send_buffer, "quit") != 0);

	//7) Отключение от сервера:
	send(connect_socket, "quit", 4, 0);
	iResult = shutdown(connect_socket, SD_SEND);
	if (iResult == SOCKET_ERROR)
	{
		dwLastError = WSAGetLastError();
		cout << "Shutdown failed with error: " << dwLastError << endl;
	}
	closesocket(connect_socket);
	freeaddrinfo(result);
	WSACleanup();

	return dwLastError;
}