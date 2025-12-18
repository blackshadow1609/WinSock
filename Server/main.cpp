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
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

using namespace std;

#pragma comment(lib, "WS2_32.lib")

#define DEFAULT_PORT "27015"
#define BUFFER_LENGTH 32768  
#define MAX_CLIENTS		 3
#define g_sz_SORRY "Error: Количество подключений превышено"
#define IP_STR_MAX_LENGTH 16

struct ClientInfo {
    SOCKET socket;
    DWORD threadId;
    HANDLE threadHandle;
    string address;
    int port;
    string nickname;
    bool active;
};

// Глобальные переменные
CRITICAL_SECTION cs;
ClientInfo clients[MAX_CLIENTS];
INT activeClients = 0;

VOID WINAPI HandleClient(LPVOID lpParam);
VOID BroadcastMessage(const string& message, SOCKET sender_socket, const string& sender_name = "");
VOID PrintClientsInfo();
VOID SendSystemMessage(SOCKET socket, const string& message);
string GetCurrentTime();
int FindFreeSlot();
int FindClientBySocket(SOCKET socket);
int FindClientByThreadId(DWORD threadId);
void RemoveClient(int index);

int main()
{
    setlocale(LC_ALL, "Russian");
    DWORD dwLastError = 0;
    INT iResult = 0;

    InitializeCriticalSection(&cs);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = INVALID_SOCKET;
        clients[i].active = false;
        clients[i].nickname = "Гость";
    }

    // 0) Инициализация WS:
    WSADATA wsaData;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        cout << "WSA init failed with: " << iResult << endl;
        DeleteCriticalSection(&cs);
        return iResult;
    }

    // 1) Инициализация переменных для Сокета:
    addrinfo* result = NULL;
    addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // 2) Параметры Сокета:
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0)
    {
        cout << "getaddrinfo failed with error: " << iResult << endl;
        DeleteCriticalSection(&cs);
        WSACleanup();
        return iResult;
    }

    // 3) Создание Сокета:
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

    int opt = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // 4) Bind socket:
    iResult = bind(listen_socket, result->ai_addr, (int)result->ai_addrlen);
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

    // 5) Запуск прослушивания сокета:
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

    sockaddr_in serverAddr;
    int addrLen = sizeof(serverAddr);
    getsockname(listen_socket, (sockaddr*)&serverAddr, &addrLen);
    char serverIP[IP_STR_MAX_LENGTH];
    inet_ntop(AF_INET, &serverAddr.sin_addr, serverIP, IP_STR_MAX_LENGTH);

    cout << "==========================================" << endl;
    cout << "           ЧАТ-СЕРВЕР ЗАПУЩЕН            " << endl;
    cout << "==========================================" << endl;
    cout << "Адрес: " << serverIP << ":" << DEFAULT_PORT << endl;
    cout << "Максимальное количество клиентов: " << MAX_CLIENTS << endl;
    cout << "Буфер сообщений: " << BUFFER_LENGTH / 1024 << "KB" << endl;
    cout << "==========================================" << endl << endl;

    PrintClientsInfo();

    // 6) Обработка запроса от клиентов:
    while (true)
    {
        cout << "\nОжидание подключений..." << endl;

        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            dwLastError = WSAGetLastError();
            cout << "Accept failed with error: " << dwLastError << endl;
            continue;
        }

        EnterCriticalSection(&cs);

        int freeSlot = FindFreeSlot();
        if (freeSlot != -1 && activeClients < MAX_CLIENTS)
        {
            sockaddr_in clientAddr;
            int clientAddrLen = sizeof(clientAddr);
            getpeername(client_socket, (sockaddr*)&clientAddr, &clientAddrLen);

            char clientIP[IP_STR_MAX_LENGTH];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, IP_STR_MAX_LENGTH);
            int clientPort = ntohs(clientAddr.sin_port);

            clients[freeSlot].socket = client_socket;
            clients[freeSlot].address = clientIP;
            clients[freeSlot].port = clientPort;
            clients[freeSlot].active = true;

            clients[freeSlot].threadHandle = CreateThread(
                NULL,
                0,
                (LPTHREAD_START_ROUTINE)HandleClient,
                (LPVOID)client_socket,
                0,
                &clients[freeSlot].threadId
            );

            if (clients[freeSlot].threadHandle == NULL)
            {
                cout << "CreateThread failed: " << GetLastError() << endl;
                closesocket(client_socket);
                clients[freeSlot].socket = INVALID_SOCKET;
                clients[freeSlot].active = false;
            }
            else
            {
                activeClients++;

                string connectTime = GetCurrentTime();
                string welcomeMsg = "[СИСТЕМА] Добро пожаловать в чат!\n";
                welcomeMsg += "[СИСТЕМА] Ваш адрес: " + string(clientIP) + ":" + to_string(clientPort) + "\n";
                welcomeMsg += "[СИСТЕМА] Подключились в: " + connectTime + "\n";
                welcomeMsg += "[СИСТЕМА] Активных клиентов: " + to_string(activeClients) + "\n";
                welcomeMsg += "[СИСТЕМА] Свободных слотов: " + to_string(MAX_CLIENTS - activeClients) + "\n";
                welcomeMsg += "[СИСТЕМА] Для смены имени используйте: /name <новое имя>\n";
                welcomeMsg += "[СИСТЕМА] Для выхода: /quit или /exit";

                SendSystemMessage(client_socket, welcomeMsg);

                string systemMsg = "[СИСТЕМА] " + string(clientIP) + ":" + to_string(clientPort) +
                    " присоединился к чату (" + connectTime + ")";
                BroadcastMessage(systemMsg, client_socket, "СИСТЕМА");

                cout << "\n[СИСТЕМА] Новый клиент подключен: " << clientIP << ":" << clientPort << endl;
                PrintClientsInfo();
            }
        }
        else
        {
            cout << "[СИСТЕМА] Отклонено подключение: достигнут лимит клиентов" << endl;
            string errorMsg = "[СИСТЕМА] " + string(g_sz_SORRY) + "\n";
            errorMsg += "[СИСТЕМА] Активных клиентов: " + to_string(activeClients) + "\n";
            errorMsg += "[СИСТЕМА] Попробуйте позже";
            send(client_socket, errorMsg.c_str(), errorMsg.length(), 0);
            closesocket(client_socket);
        }

        LeaveCriticalSection(&cs);
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].threadHandle != NULL) {
            CloseHandle(clients[i].threadHandle);
        }
    }

    closesocket(listen_socket);
    freeaddrinfo(result);
    DeleteCriticalSection(&cs);
    WSACleanup();

    return 0;
}

string GetCurrentTime() {
    auto now = chrono::system_clock::now();
    auto now_time = chrono::system_clock::to_time_t(now);
    auto now_ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()) % 1000;

    tm local_tm;
    localtime_s(&local_tm, &now_time);

    stringstream ss;
    ss << put_time(&local_tm, "%H:%M:%S") << "."
        << setfill('0') << setw(3) << now_ms.count();

    return ss.str();
}

int FindFreeSlot() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active || clients[i].socket == INVALID_SOCKET) {
            return i;
        }
    }
    return -1;
}

int FindClientBySocket(SOCKET socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].socket == socket) {
            return i;
        }
    }
    return -1;
}

int FindClientByThreadId(DWORD threadId) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].threadId == threadId) {
            return i;
        }
    }
    return -1;
}

void RemoveClient(int index) {
    if (index < 0 || index >= MAX_CLIENTS) return;

    if (clients[index].socket != INVALID_SOCKET) {
        closesocket(clients[index].socket);
    }

    clients[index].socket = INVALID_SOCKET;
    clients[index].active = false;
    clients[index].address.clear();
    clients[index].port = 0;
    clients[index].nickname = "Гость";

    if (activeClients > 0) {
        activeClients--;
    }
}

VOID SendSystemMessage(SOCKET socket, const string& message) {
    string formattedMsg = "\n" + message + "\n";
    send(socket, formattedMsg.c_str(), formattedMsg.length(), 0);
}

VOID BroadcastMessage(const string& message, SOCKET sender_socket, const string& sender_name) {
    EnterCriticalSection(&cs);

    string formattedMsg;

    string sender_display;
    if (sender_name == "СИСТЕМА") {
        formattedMsg = "\n" + message + "\n";
    }
    else {
        int senderIndex = FindClientBySocket(sender_socket);
        if (senderIndex != -1 && !clients[senderIndex].nickname.empty()) {
            sender_display = clients[senderIndex].nickname;
        }
        else {
            sender_display = "Неизвестный";
        }

        string timestamp = GetCurrentTime();
        formattedMsg = "\n[" + timestamp + "] " + sender_display + ": " + message + "\n";
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].socket != INVALID_SOCKET &&
            clients[i].socket != sender_socket) {
            send(clients[i].socket, formattedMsg.c_str(), formattedMsg.length(), 0);
        }
    }

    if (sender_name == "СИСТЕМА") {
        cout << message << endl;
    }
    else {
        cout << formattedMsg;
    }

    LeaveCriticalSection(&cs);
}

VOID PrintClientsInfo() {
    EnterCriticalSection(&cs);

    cout << "\n==========================================" << endl;
    cout << "         ИНФОРМАЦИЯ О КЛИЕНТАХ          " << endl;
    cout << "==========================================" << endl;
    cout << "Активных клиентов: " << activeClients << endl;
    cout << "Свободных слотов: " << (MAX_CLIENTS - activeClients) << endl;
    cout << "Буфер сообщений: " << BUFFER_LENGTH / 1024 << "KB" << endl;
    cout << "------------------------------------------" << endl;

    if (activeClients == 0) {
        cout << "Нет активных подключений" << endl;
    }
    else {
        cout << "Список клиентов:" << endl;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                cout << "  [" << (i + 1) << "] " << clients[i].nickname << " ("
                    << clients[i].address << ":" << clients[i].port << ")" << endl;
            }
        }
    }

    cout << "==========================================" << endl;

    LeaveCriticalSection(&cs);
}

VOID WINAPI HandleClient(LPVOID lpParam) {
    SOCKET client_socket = (SOCKET)lpParam;

    int clientIndex = FindClientBySocket(client_socket);
    if (clientIndex == -1) {
        ExitThread(0);
        return;
    }

    CHAR recv_buffer[BUFFER_LENGTH] = {};
    INT iResult = 0;

    while (true) {
        ZeroMemory(recv_buffer, BUFFER_LENGTH);
        iResult = recv(client_socket, recv_buffer, BUFFER_LENGTH, 0);

        if (iResult > 0) {
            string message = recv_buffer;

            message.erase(remove(message.begin(), message.end(), '\r'), message.end());
            while (!message.empty() && (message.back() == '\n' || message.back() == ' ')) {
                message.pop_back();
            }

            if (!message.empty()) {
                if (message.find("/name ") == 0 || message.find("/nick ") == 0) {
                    size_t spacePos = message.find(' ');
                    if (spacePos != string::npos) {
                        string newName = message.substr(spacePos + 1);
                        if (!newName.empty()) {
                            EnterCriticalSection(&cs);
                            string oldName = clients[clientIndex].nickname;
                            clients[clientIndex].nickname = newName;

                            string systemMsg = "[СИСТЕМА] " + oldName + " сменил имя на: " + newName;
                            BroadcastMessage(systemMsg, client_socket, "СИСТЕМА");

                            string confirmMsg = "[СИСТЕМА] Ваше имя изменено на: " + newName;
                            SendSystemMessage(client_socket, confirmMsg);

                            cout << "[СИСТЕМА] Клиент " << clients[clientIndex].address << ":"
                                << clients[clientIndex].port << " сменил имя на: " << newName << endl;

                            PrintClientsInfo();
                            LeaveCriticalSection(&cs);
                        }
                    }
                }
                else if (message == "/list" || message == "/who") {
                    EnterCriticalSection(&cs);
                    string listMsg = "[СИСТЕМА] Активных клиентов: " + to_string(activeClients) + "\n";
                    listMsg += "[СИСТЕМА] Список участников:\n";

                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (clients[i].active) {
                            listMsg += "[СИСТЕМА]   " + clients[i].nickname + " (" +
                                clients[i].address + ":" + to_string(clients[i].port) + ")\n";
                        }
                    }

                    SendSystemMessage(client_socket, listMsg);
                    LeaveCriticalSection(&cs);
                }
                else if (message == "/quit" || message == "/exit" ||
                    message == "quit" || message == "exit") {
                    break;
                }
                else if (message == "/help" || message == "/?") {
                    string helpMsg = "[СИСТЕМА] Доступные команды:\n";
                    helpMsg += "[СИСТЕМА]   /name <имя> - сменить имя\n";
                    helpMsg += "[СИСТЕМА]   /list - показать список участников\n";
                    helpMsg += "[СИСТЕМА]   /help - эта справка\n";
                    helpMsg += "[СИСТЕМА]   /quit - выйти из чата";
                    SendSystemMessage(client_socket, helpMsg);
                }
                else {
                    BroadcastMessage(message, client_socket, clients[clientIndex].nickname);
                }
            }
        }
        else if (iResult == 0) {
            break;
        }
        else {
            break;
        }
    }

    EnterCriticalSection(&cs);

    if (clientIndex != -1 && clients[clientIndex].active) {
        string disconnectMsg = "[СИСТЕМА] " + clients[clientIndex].nickname + " (" +
            clients[clientIndex].address + ":" + to_string(clients[clientIndex].port) +
            ") покинул чат";

        BroadcastMessage(disconnectMsg, client_socket, "СИСТЕМА");

        cout << "[СИСТЕМА] Клиент отключен: " << clients[clientIndex].address << ":"
            << clients[clientIndex].port << endl;

        RemoveClient(clientIndex);
        PrintClientsInfo();
    }

    LeaveCriticalSection(&cs);

    ExitThread(0);
}