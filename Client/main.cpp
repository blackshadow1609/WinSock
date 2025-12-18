#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <iostream>
#include <thread>
#include <atomic>

using namespace std;

#pragma comment(lib, "WS2_32.lib")

#define DEFAULT_PORT "27015"
#define BUFFER_LENGTH 1460

atomic<bool> g_bReceiving(true);

DWORD WINAPI ReceiveThread(LPVOID lpParam) {
    SOCKET connect_socket = (SOCKET)lpParam;
    CHAR recv_buffer[BUFFER_LENGTH] = {};

    while (g_bReceiving) {
        ZeroMemory(recv_buffer, BUFFER_LENGTH);
        INT iResult = recv(connect_socket, recv_buffer, BUFFER_LENGTH, 0);

        if (iResult > 0) {
            cout << "\n[Сервер]: " << recv_buffer << endl;
            cout << "Введите сообщение: ";
            cout.flush();
        }
        else if (iResult == 0) {
            cout << "\nСервер закрыл соединение" << endl;
            g_bReceiving = false;
            break;
        }
        else {
            INT error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                cout << "\nОшибка приема: " << error << endl;
                g_bReceiving = false;
                break;
            }
        }

        Sleep(100); 
    }

    return 0;
}

int main()
{
    setlocale(LC_ALL, "Russian");

    cout << "Клиент чата запущен" << endl;
    cout << "Для выхода введите 'quit' или 'exit'" << endl << endl;

    INT iResult = 0;
    DWORD dwLastError = 0;

    // 0) Инициализация:
    WSADATA wsaData;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        cout << "WSAStartup failed: " << iResult << endl;
        return iResult;
    }

    // 1) Переменные для хранения информации о Сокете:
    addrinfo* result = NULL;
    addrinfo* ptr = NULL;
    addrinfo hints = { 0 };
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Запрос IP-адреса сервера
    CHAR server_ip[16];
    cout << "Введите IP-адрес сервера (по умолчанию 127.168.1.0): ";
    cin.getline(server_ip, 16);
    if (strlen(server_ip) == 0) {
        strcpy_s(server_ip, "192.168.1.100");
    }

    // 2) Информация о сервере к которому будет подключение:
    iResult = getaddrinfo(server_ip, DEFAULT_PORT, &hints, &result);
    if (iResult != 0)
    {
        cout << "getaddrinfo failed: " << iResult << endl;
        WSACleanup();
        return iResult;
    }

    // 3) Создание Сокета для подключения к серверу:
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

    u_long mode = 0; 
    ioctlsocket(connect_socket, FIONBIO, &mode);

    // 4) Подключение к серверу:
    cout << "Подключение к серверу " << server_ip << ":" << DEFAULT_PORT << "..." << endl;

    iResult = connect(connect_socket, ptr->ai_addr, (INT)ptr->ai_addrlen);
    if (iResult == SOCKET_ERROR)
    {
        dwLastError = WSAGetLastError();
        cout << "Connection error: " << dwLastError << endl;
        cout << "Убедитесь, что сервер запущен и доступен по адресу " << server_ip << endl;
        closesocket(connect_socket);
        freeaddrinfo(result);
        WSACleanup();
        return dwLastError;
    }

    cout << "Успешное подключение к серверу!" << endl;

    HANDLE hReceiveThread = CreateThread(NULL, 0, ReceiveThread, (LPVOID)connect_socket, 0, NULL);
    if (hReceiveThread == NULL) {
        cout << "Не удалось создать поток приема: " << GetLastError() << endl;
    }
    else {
        cout << "Поток приема сообщений запущен" << endl;
    }

    // 5) Отправка данных на сервер:
    CHAR send_buffer[BUFFER_LENGTH] = {};

    Sleep(500);

    cout << "\nВведите сообщение: ";

    do
    {
        ZeroMemory(send_buffer, BUFFER_LENGTH);

        SetConsoleCP(1251);
        cin.getline(send_buffer, BUFFER_LENGTH);
        SetConsoleCP(866);

        if (strstr(send_buffer, "exit") != NULL || strstr(send_buffer, "quit") != NULL) {
            iResult = send(connect_socket, send_buffer, strlen(send_buffer), 0);
            break;
        }

        if (strlen(send_buffer) > 0) {
            iResult = send(connect_socket, send_buffer, strlen(send_buffer), 0);
            if (iResult == SOCKET_ERROR)
            {
                dwLastError = WSAGetLastError();
                cout << "Send failed with error: " << dwLastError << endl;
                break;
            }
            cout << "Отправлено " << iResult << " байт" << endl;
        }
        else {
            cout << "Введите сообщение: ";
        }

    } while (g_bReceiving);

    g_bReceiving = false;

    if (hReceiveThread != NULL) {
        WaitForSingleObject(hReceiveThread, 2000);
        CloseHandle(hReceiveThread);
    }

    cout << "Отключение от сервера..." << endl;

    iResult = shutdown(connect_socket, SD_SEND);
    if (iResult == SOCKET_ERROR)
    {
        dwLastError = WSAGetLastError();
        cout << "Shutdown failed with error: " << dwLastError << endl;
    }

    Sleep(500);

    closesocket(connect_socket);
    freeaddrinfo(result);
    WSACleanup();

    cout << "Клиент завершил работу. Нажмите Enter для выхода...";
    cin.get();

    return 0;
}