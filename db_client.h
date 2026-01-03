#ifndef DB_CLIENT_H
#define DB_CLIENT_H

#include <string>
#include <mutex>
#include "Vector.h"
#include "structures.h"

class DatabaseClient {
private:
    int sock;
    std::string host;
    int port;
    bool connected;
    std::mutex socketMutex;
    
public:
    DatabaseClient(const std::string& host = "127.0.0.1", int port = 7432);
    ~DatabaseClient();
    
    bool connectToDB();
    void disconnect();
    std::string sendCommand(const std::string& command);
    Vector<std::string> executeSelect(const Vector<std::string>& columns, 
                                     const Vector<std::string>& tables, 
                                     const Vector<Condition>& conditions = Vector<Condition>());
    bool executeInsert(const std::string& table, const std::string& values);
    bool executeDelete(const std::string& table, const Vector<Condition>& conditions);
    int getMaxOrderId();
    bool isConnected() const;
};

#endif // DB_CLIENT_H