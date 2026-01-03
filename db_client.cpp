#include "db_client.h"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <netdb.h>

using namespace std;

DatabaseClient::DatabaseClient(const string& host, int port) 
    : host(host), port(port), connected(false), sock(-1) {}

DatabaseClient::~DatabaseClient() {
    disconnect();
}

bool DatabaseClient::connectToDB() {
    lock_guard<mutex> lock(socketMutex);

    if (connected && sock != -1) {
        char test;
        int result = recv(sock, &test, 1, MSG_PEEK | MSG_DONTWAIT);
        if (result >= 0 || (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            return true;
        }
    }

    if (sock != -1) {
        close(sock);
        sock = -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "[DB] Ошибка создания сокета: " << strerror(errno) << endl;
        return false;
    }

    struct timeval timeout{5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(host.c_str(), to_string(port).c_str(), &hints, &res);
    if (status != 0) {
        cerr << "[DB] Невозможно разрешить хост: " << host << " (" << gai_strerror(status) << ")" << endl;
        close(sock);
        sock = -1;
        return false;
    }

    if (::connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        cerr << "[DB] Не удалось подключиться к СУБД: " << strerror(errno) << endl;
        freeaddrinfo(res);
        close(sock);
        sock = -1;
        return false;
    }

    freeaddrinfo(res);
    connected = true;
    cout << "[DB] Успешно подключено к СУБД на " << host << ":" << port << endl;
    return true;
}

void DatabaseClient::disconnect() {
    lock_guard<mutex> lock(socketMutex);
    
    if (connected) {
        try {
            string exitCmd = "EXIT\n";
            send(sock, exitCmd.c_str(), exitCmd.length(), 0);
        } 
        catch (...) {
        }
        close(sock);
        connected = false;
        sock = -1;
    }
}

string DatabaseClient::sendCommand(const string& command) {
    lock_guard<mutex> lock(socketMutex);
    
    if (!connected && !connectToDB()) {
        throw runtime_error("Не удалось подключиться к СУБД");
    }
    
    string cmdWithNewline = command + "\n";
    ssize_t sent = send(sock, cmdWithNewline.c_str(), cmdWithNewline.length(), 0);
    if (sent < 0) {
        connected = false;
        throw runtime_error("Ошибка отправки команды: " + string(strerror(errno)));
    }
    
    string response;
    char buffer[4096];
    bool endFound = false;
    
    while (!endFound) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesRead < 0) {
            connected = false;
            throw runtime_error("Ошибка чтения ответа: " + string(strerror(errno)));
        }
        
        if (bytesRead == 0) {
            connected = false;
            throw runtime_error("Соединение разорвано");
        }
        
        response.append(buffer, bytesRead);
        
        if (response.find("\nEND\n") != string::npos) {
            endFound = true;
        }
    }
    
    size_t endPos = response.find("\nEND\n");
    if (endPos != string::npos) {
        response = response.substr(0, endPos);
    }
    
    return response;
}

Vector<string> DatabaseClient::executeSelect(const Vector<string>& columns, const Vector<string>& tables, 
                                            const Vector<Condition>& conditions) {
    string cmd = "SELECT ";
    
    for (size_t i = 0; i < columns.get_size(); i++) {
        cmd += columns[i];
        if (i + 1 < columns.get_size()) cmd += ", ";
    }
    
    cmd += " FROM ";
    for (size_t i = 0; i < tables.get_size(); i++) {
        cmd += tables[i];
        if (i + 1 < tables.get_size()) cmd += ", ";
    }
    
    if (conditions.get_size() > 0) {
        cmd += " WHERE ";
        bool firstCondition = true;
        
        for (size_t i = 0; i < conditions.get_size(); i++) {
            const Condition& cond = conditions[i];
            
            if (cond.column == "(" || cond.column == ")") {
                cmd += cond.column;
                continue;
            }
            
            if (!cond.logicalOperator.empty()) {
                cmd += " " + cond.logicalOperator + " ";
            } else if (!firstCondition) {
                cmd += " AND ";
            }
            
            if (!cond.column.empty() && !cond.value.empty()) {
                cmd += cond.column + "=";
                
                bool isNumber = true;
                for (char c : cond.value) {
                    if (!isdigit(c) && c != '.' && c != '-') {
                        isNumber = false;
                        break;
                    }
                }
                
                if (isNumber) {
                    cmd += cond.value;
                } else {
                    cmd += "'" + cond.value + "'";
                }
                firstCondition = false;
            }
        }
    }
    
    string response = sendCommand(cmd);
    
    Vector<string> results;
    stringstream responseStream(response);
    string line;
    
    int lineCount = 0;
    
    while (getline(responseStream, line)) {
        lineCount++;
        
        if (lineCount == 1) {
            continue;
        }
        
        if (!line.empty() && line != "Empty result") {
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            
            if (!line.empty()) {
                results.push_back(line);
            }
        }
    }
        
    return results;
}

bool DatabaseClient::executeInsert(const string& table, const string& values) {
    string cmd = "INSERT INTO " + table + " VALUES " + values;
    string response = sendCommand(cmd);
    
    return response.empty();
}

bool DatabaseClient::executeDelete(const string& table, const Vector<Condition>& conditions) {
    string cmd = "DELETE FROM " + table;
    
    if (conditions.get_size() > 0) {
        cmd += " WHERE ";
        
        for (size_t i = 0; i < conditions.get_size(); i++) {
            const Condition& cond = conditions[i];
            
            if (i > 0) cmd += " AND ";
            cmd += cond.column + "=";
            
            bool isNumber = true;
            for (char c : cond.value) {
                if (!isdigit(c) && c != '.' && c != '-') {
                    isNumber = false;
                    break;
                }
            }
            
            if (isNumber) {
                cmd += cond.value;
            } else {
                cmd += "'" + cond.value + "'";
            }
        }
    }
    
    string response = sendCommand(cmd);
    return response.find("OK") != string::npos;
}

int DatabaseClient::getMaxOrderId() {
    try {
        Vector<string> columns = {"order.order_id"};
        Vector<string> tables = {"order"};
        Vector<string> results = executeSelect(columns, tables);
        
        if (results.get_size() == 0) {
            return 0;
        }
        
        int maxId = 0;
        for (size_t i = 0; i < results.get_size(); i++) {
            try {
                int currentId = stoi(results[i]);
                if (currentId > maxId) {
                    maxId = currentId;
                }
            } catch (...) {
                continue;
            }
        }
        
        return maxId;
    } catch (const exception& e) {
        cerr << "Ошибка при получении максимального ID ордера: " << e.what() << endl;
        return 1;
    }
}

bool DatabaseClient::isConnected() const {
    return connected;
}