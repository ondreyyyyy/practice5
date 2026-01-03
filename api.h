#ifndef API_H
#define API_H

#include <string>
#include "structures.h"
#include "nlohmann/json.hpp"

class DatabaseClient;

std::string handleGetLots(DatabaseManager& dbManager, DatabaseClient& dbClient);
std::string handleGetPairs(DatabaseManager& dbManager, DatabaseClient& dbClient);
std::string handleCreateUser(DatabaseManager& dbManager, const std::string& body, DatabaseClient& dbClient);
std::string handleGetBalance(DatabaseManager& dbManager, const std::string& userKey, DatabaseClient& dbClient);
std::string handleGetOrders(DatabaseManager& dbManager, DatabaseClient& dbClient);
std::string handleCreateOrder(DatabaseManager& dbManager, const std::string& body, const std::string& userKey, DatabaseClient& dbClient);
std::string handleDeleteOrder(DatabaseManager& dbManager, const std::string& body, const std::string& userKey, DatabaseClient& dbClient);
std::string makeHttpResponse(int statusCode, const std::string& body);
std::string getUserIdByKey(const std::string& userKey, DatabaseClient& dbClient);
std::string generateUserKey();
PairInfo getPairInfo(const std::string& pairId, DatabaseClient& dbClient);
double getUserBalance(const std::string& userId, const std::string& lotId, DatabaseClient& dbClient);
void updateUserBalance(const std::string& userId, const std::string& lotId, double delta, DatabaseClient& dbClient);
bool updateOrderQuantity(const std::string& orderId, double newQuantity, DatabaseClient& dbClient);
bool closeOrderWithTimestamp(const std::string& orderId, const std::string& timestamp, DatabaseClient& dbClient);
void unlockFundsForOrder(const std::string& userId, const std::string& orderType, 
                         const std::string& assetLot, const std::string& currencyLot, 
                         double quantity, double price, DatabaseClient& dbClient);
bool canDeleteOrder(const std::string& orderId, const std::string& userId, DatabaseClient& dbClient);

#endif // API_H