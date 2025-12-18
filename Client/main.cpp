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
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

#pragma comment(lib, "WS2_32.lib")

#define DEFAULT_PORT "27015"
#define BUFFER_LENGTH 32768

atomic<bool> g_bReceiving(true);
atomic<bool> g_bConnected(false);
string g_sNickname = "Гость";

void ClearScreen() {
    system("cls");
}

void DisplayHeader() {
    cout << "==========================================" << endl;
    cout << "             КЛИЕНТ ЧАТА                 " << endl;
    cout << "==========================================" << endl;
    cout << "Буфер сообщений: " << BUFFER_LENGTH / 1024 << "KB" << endl;
    cout << "Текущее имя: " << g_sNickname << endl;
    cout << "Для справки введите: /help" << endl;
    cout << "==========================================" << endl << endl;
}

void DisplayMessage(const string& message, bool isOwn = false) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);

    if (message.find("[СИСТЕМА]") == 0) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }
    else if (isOwn) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }
    else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }

    cout << message;

    SetConsoleTextAttribute(hConsole, csbi.wAttributes);
}

void DisplayPrompt() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);

    COORD pos;
    pos.X = 0;
    pos.Y = csbi.dwCursorPosition.Y + 1;
    SetConsoleCursorPosition(hConsole, pos);

    cout << "[" << g_sNickname << "]: ";
    cout.flush();

    SetConsoleTextAttribute(hConsole, csbi.wAttributes);
}

DWORD WINAPI ReceiveThread(LPVOID lpParam) {
    SOCKET connect_socket = (SOCKET)lpParam;
    CHAR recv_buffer[BUFFER_LENGTH] = {};

    while (g_bReceiving && g_bConnected) {
        ZeroMemory(recv_buffer, BUFFER_LENGTH);
        INT iResult = recv(connect_socket, recv_buffer, BUFFER_LENGTH, 0);

        if (iResult > 0) {
            string message = recv_buffer;

            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hConsole, &csbi);
            COORD pos;
            pos.X = 0;
            pos.Y = csbi.dwCursorPosition.Y;
            if (csbi.dwCursorPosition.X > 0) {
                pos.Y++;
            }

            SetConsoleCursorPosition(hConsole, pos);
            bool isSystem = (message.find("[СИСТЕМА]") == 0);
            DisplayMessage(message, false);
            GetConsoleScreenBufferInfo(hConsole, &csbi);
            pos.X = 0;
            pos.Y = csbi.dwCursorPosition.Y;
            SetConsoleCursorPosition(hConsole, pos);

            DisplayPrompt();

            string prompt = "[" + g_sNickname + "]: ";
            pos.X = (SHORT)prompt.length();
            SetConsoleCursorPosition(hConsole, pos);
        }
        else if (iResult == 0) {
            DisplayMessage("\n[СИСТЕМА] Сервер закрыл соединение\n", false);
            g_bConnected = false;
            break;
        }
        else {
            INT error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                DisplayMessage("\n[СИСТЕМА] Ошибка соединения: " + to_string(error) + "\n", false);
                g_bConnected = false;
                break;
            }
        }

        Sleep(50);
    }

    return 0;
}

int main()
{
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    ClearScreen();
    DisplayHeader();

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

    CHAR server_ip[16];
    cout << "Введите IP-адрес сервера: ";
    cin.getline(server_ip, 16);
    if (strlen(server_ip) == 0) {
        strcpy_s(server_ip, "127.0.0.1");
    }

    CHAR nickname[32];
    cout << "Введите ваш никнейм: ";
    cin.getline(nickname, 32);
    if (strlen(nickname) > 0) {
        g_sNickname = nickname;
    }

    ClearScreen();
    DisplayHeader();

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

    int timeout = 5000; 
    setsockopt(connect_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(connect_socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

    // 4) Подключение к серверу:
    DisplayMessage("Подключение к серверу " + string(server_ip) + ":" + DEFAULT_PORT + "...\n", false);

    iResult = connect(connect_socket, ptr->ai_addr, (INT)ptr->ai_addrlen);
    if (iResult == SOCKET_ERROR)
    {
        dwLastError = WSAGetLastError();
        DisplayMessage("Ошибка подключения: " + to_string(dwLastError) + "\n", false);
        DisplayMessage("Убедитесь, что сервер запущен по адресу " + string(server_ip) + "\n", false);
        closesocket(connect_socket);
        freeaddrinfo(result);
        WSACleanup();

        cout << "\nНажмите Enter для выхода...";
        cin.get();
        return dwLastError;
    }

    g_bConnected = true;
    DisplayMessage("Успешное подключение к серверу!\n", false);
    DisplayMessage("Для справки введите: /help\n\n", false);

    HANDLE hReceiveThread = CreateThread(NULL, 0, ReceiveThread, (LPVOID)connect_socket, 0, NULL);
    if (hReceiveThread == NULL) {
        DisplayMessage("Не удалось создать поток приема: " + to_string(GetLastError()) + "\n", false);
    }

    string initMsg = "/name " + g_sNickname;
    send(connect_socket, initMsg.c_str(), initMsg.length(), 0);

    DisplayPrompt();

    // 5) Основной цикл отправки сообщений:
    CHAR send_buffer[BUFFER_LENGTH] = {};

    while (g_bConnected)
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);

        string input;
        char ch;

        while (true) {
            ch = _getch();

            if (ch == '\r') { 
                cout << endl;
                break;
            }
            else if (ch == '\b') { 
                if (!input.empty()) {
                    input.pop_back();

                    COORD pos = csbi.dwCursorPosition;
                    if (pos.X > 0) {
                        pos.X--;
                        SetConsoleCursorPosition(hConsole, pos);
                        cout << " ";
                        SetConsoleCursorPosition(hConsole, pos);
                    }

                    GetConsoleScreenBufferInfo(hConsole, &csbi);
                }
            }
            else if (ch == 27) { 
                input = "/quit";
                cout << endl;
                break;
            }
            else if (ch >= 32 && ch <= 126 || ch < 0) { 
                if (input.length() < BUFFER_LENGTH - 1) {
                    input += ch;
                    cout << ch;
                    GetConsoleScreenBufferInfo(hConsole, &csbi);
                }
            }
        }

        if (!input.empty()) {
            if (input == "/clear" || input == "/cls") {
                ClearScreen();
                DisplayHeader();
                DisplayPrompt();
                continue;
            }
            else if (input == "/help" || input == "/?") {
                DisplayMessage("\n[СИСТЕМА] Доступные команды:\n", false);
                DisplayMessage("[СИСТЕМА]   /name <имя> - сменить имя\n", false);
                DisplayMessage("[СИСТЕМА]   /list - показать список участников\n", false);
                DisplayMessage("[СИСТЕМА]   /clear - очистить экран\n", false);
                DisplayMessage("[СИСТЕМА]   /help - эта справка\n", false);
                DisplayMessage("[СИСТЕМА]   /quit - выйти из чата\n\n", false);
                DisplayPrompt();
                continue;
            }
            else if (input.find("/name ") == 0) {
                size_t spacePos = input.find(' ');
                if (spacePos != string::npos) {
                    string newName = input.substr(spacePos + 1);
                    if (!newName.empty()) {
                        g_sNickname = newName;
                    }
                }
            }

            iResult = send(connect_socket, input.c_str(), input.length(), 0);
            if (iResult == SOCKET_ERROR)
            {
                dwLastError = WSAGetLastError();
                DisplayMessage("\nОшибка отправки: " + to_string(dwLastError) + "\n", false);
                break;
            }

            if (input == "/quit" || input == "/exit" || input == "quit" || input == "exit") {
                break;
            }

            DisplayPrompt();
        }
    }

    g_bReceiving = false;
    g_bConnected = false;

    if (hReceiveThread != NULL) {
        WaitForSingleObject(hReceiveThread, 2000);
        CloseHandle(hReceiveThread);
    }

    DisplayMessage("\nОтключение от сервера...\n", false);

    iResult = shutdown(connect_socket, SD_SEND);
    if (iResult == SOCKET_ERROR)
    {
        dwLastError = WSAGetLastError();
        DisplayMessage("Shutdown failed with error: " + to_string(dwLastError) + "\n", false);
    }

    Sleep(500);

    closesocket(connect_socket);
    freeaddrinfo(result);
    WSACleanup();

    DisplayMessage("Клиент завершил работу. Нажмите Enter для выхода...", false);
    cin.get();

    return 0;
}