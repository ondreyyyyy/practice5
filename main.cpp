#include <iostream>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include "interface.h"
#include "structures.h"
#include "file.h"
#include "Vector.h"

using namespace std;

void handleClient(int clientSocket, DatabaseManager& dbManager, mutex& dbMutex) {
    char buffer[4096] = {0};

    cout << "Клиент подключен." << endl;

    try {
        while (true) {
            memset(buffer, 0, sizeof(buffer));

            int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

            if (bytesRead <= 0) {
                if (bytesRead == 0) {
                    cout << "Клиент отключился." << endl;
                } else {
                    cerr << "Ошибка чтения от клиента." << endl;
                }
                break;
            }

            string command(buffer, bytesRead);

            if (!command.empty() && command.back() == '\n') {
                command.pop_back();
            }

            cout << "Получена команда от клиента: " << command << endl;

            string response;
            {
                lock_guard<mutex> lock(dbMutex);
                response = processCommand(dbManager, command);
            }

            if (response == "EXIT") {
                string goodbye = "Завершение сессии\n";
                send(clientSocket, goodbye.c_str(), goodbye.length(), 0);
                break;
            }

            if (!response.empty()) {
                send(clientSocket, response.c_str(), response.length(), 0);
            }

            string endMarker = "\nEND\n";
            send(clientSocket, endMarker.c_str(), endMarker.length(), 0);
        }
    }
    catch (const exception& e) {
        cerr << "Ошибка при обработке клиента: " << e.what() << endl;
    }

    close(clientSocket);
    cout << "Соединение закрыто." << endl;
}

int main() {
    DatabaseManager dbManager;
    mutex dbMutex;

    // cout << "Введите название JSON файла со схемой: ";
    // string schemaFile;
    // getline(cin, schemaFile);

    try {
        initCryptoDatabase(dbManager);
    }
    catch (const exception& e) {
        cerr << "Ошибка загрузки схемы: " << e.what() << endl;
        return 1;
    }

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        cerr << "Ошибка создания сокета" << endl;
        return 1;
    }

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(7432);

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0) {
        cerr << "Ошибка bind()" << endl;
        close(serverSocket);
        return 1;
    }

    if (listen(serverSocket, 10) < 0) {
        cerr << "Ошибка listen()" << endl;
        close(serverSocket);
        return 1;
    }

    cout << "Сервер запущен на порту 7432" << endl;
    cout << "Ожидание подключений." << endl;

    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientAddrLen = sizeof(clientAddress);

        int clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientAddrLen);

        if (clientSocket < 0) {
            cerr << "Ошибка соединения." << endl;
            continue; 
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, clientIP, INET_ADDRSTRLEN);

        cout << "Новое подключение: " << clientIP << ":" << ntohs(clientAddress.sin_port) << endl;

        thread(handleClient, clientSocket, ref(dbManager), ref(dbMutex)).detach();
    }

    close(serverSocket);
    return 0;
}