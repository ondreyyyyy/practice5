#include "api.h"
#include "db_client.h"
#include "select.h"
#include "auxiliary.h"
#include "structures.h"
#include <random>
#include <sstream>
#include <cmath>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cctype>

using json = nlohmann::json;
using namespace std;

const double EPSILON = 0.000001;

string makeHttpResponse(int statusCode, const string& body) {
    string statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 201: statusText = "Created"; break;
        case 400: statusText = "Bad Request"; break;
        case 401: statusText = "Unauthorized"; break;
        case 403: statusText = "Forbidden"; break;
        case 404: statusText = "Not Found"; break;
        case 500: statusText = "Internal Server Error"; break;
        default: statusText = "Unknown";
    }

    return
        "HTTP/1.1 " + to_string(statusCode) + " " + statusText + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;
}

string getUserIdByKey(const string& userKey, DatabaseClient& dbClient) {
    try {
        string safeKey = escape(userKey);
        Vector<string> columns = {"user.user_id"};
        Vector<string> tables = {"user"};
        Vector<Condition> conditions;
        conditions.push_back(Condition{"user.key", userKey, ""});
        
        Vector<string> results = dbClient.executeSelect(columns, tables, conditions);
        
        if (results.get_size() > 0 && !results[0].empty()) {
            string result = results[0];
            
            while (!result.empty() && (result.back() == ' ' || result.back() == '\n' || result.back() == '\r')) {
                result.pop_back();
            }
            
            return result;
        }
        return "";
    }
    catch (const exception& e) {
        cerr << "Ошибка при получении ID пользователя: " << e.what() << endl;
        return "";
    }
}

string generateUserKey() {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const int keyLength = 32;
    string key;
    
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);
    
    for (int i = 0; i < keyLength; ++i) {
        key += alphanum[dis(gen)];
    }
    
    return key;
}

int safe_stoi(const string& str, int defaultValue = 0) {
    if (str.empty()) return defaultValue;
    try {
        return stoi(str);
    } catch (...) {
        return defaultValue;
    }
}

double safe_stod(const string& str, double defaultValue = 0.0) {
    if (str.empty()) return defaultValue;
    try {
        return stod(str);
    } catch (...) {
        return defaultValue;
    }
}

Vector<string> parseCSVLine(const string& line) {
    Vector<string> result;
    stringstream ss(line);
    string token;
    
    while (getline(ss, token, ',')) {
        result.push_back(token);
    }
    
    return result;
}

string handleGetLots(DatabaseManager& dbManager, DatabaseClient& dbClient) {
    try {
        cout << "[INFO] Запрос списка лотов" << endl;
        
        Vector<string> columns = {"lot.lot_id", "lot.name"};
        Vector<string> tables = {"lot"};
        
        Vector<string> results = dbClient.executeSelect(columns, tables);
        
        json response = json::array();

        for (size_t i = 0; i < results.get_size(); i++) {
            Vector<string> fields = parseCSVLine(results[i]);
            if (fields.get_size() >= 2) {
                json lot;
                lot["lot_id"] = safe_stoi(fields[0]);
                lot["name"] = fields[1];
                response.push_back(lot);
            }
        }
        return makeHttpResponse(200, response.dump(4));
    }
    catch (const exception& e) {
        json error;
        error["error"] = "Internal server error";
        error["message"] = e.what();
        return makeHttpResponse(500, error.dump(4));
    }
}

string handleGetPairs(DatabaseManager& dbManager, DatabaseClient& dbClient) {
    try {
        cout << "[INFO] Запрос списка пар" << endl;
        
        Vector<string> columns = {"pair.pair_id", "pair.first_lot_id", "pair.second_lot_id"};
        Vector<string> tables = {"pair"};
        
        Vector<string> results = dbClient.executeSelect(columns, tables);

        json response = json::array();

        for (size_t i = 0; i < results.get_size(); i++) {
            Vector<string> fields = parseCSVLine(results[i]);
            if (fields.get_size() >= 3) {
                json pair;
                pair["pair_id"] = safe_stoi(fields[0]);
                pair["sale_lot_id"] = safe_stoi(fields[1]);
                pair["buy_lot_id"] = safe_stoi(fields[2]);
                response.push_back(pair);
            }
        }

        return makeHttpResponse(200, response.dump(4));
    }
    catch (const exception& e) {
        json error;
        error["error"] = "Internal server error";
        error["message"] = e.what();
        return makeHttpResponse(500, error.dump(4));
    }
}

bool isStringEmptyOrWhitespace(const string& str) {
    for (char c : str) {
        if (!isspace(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

string handleCreateUser(DatabaseManager& dbManager, const string& body, DatabaseClient& dbClient) {
    try {
        cout << "[INFO] Создание пользователя: " << body << endl;
        json request = parseJsonBody(body);

        if (!hasField(request, "username")) {
            json error = {{"error", "Не заполнено: username"}};
            return makeHttpResponse(400, error.dump(4));
        }

        string username = request["username"].get<string>();
        if (username.empty()) {
            json error = {{"error", "Username не может быть пустым"}};
            return makeHttpResponse(400, error.dump(4));
        }

        string userKey = generateUserKey();
        string safeUsername = escape(username);
        string safeKey = escape(userKey);
        
        string userQuery = "('" + safeUsername + "','" + safeKey + "')";
        if (!dbClient.executeInsert("user", userQuery)) {
            throw runtime_error("Не удалось создать пользователя");
        }

        string userId = getUserIdByKey(safeKey, dbClient);
        if (userId.empty()) {
            throw runtime_error("Пользователь не найден после создания");
        }

        Vector<string> lotColumns = {"lot.lot_id"};
        Vector<string> lotTables = {"lot"};
        Vector<string> lots = dbClient.executeSelect(lotColumns, lotTables);
        
        for (size_t i = 0; i < lots.get_size(); i++) {
            string lotIdStr = lots[i];
            size_t commaPos = lotIdStr.find(',');
            string lotId = (commaPos != string::npos) ? lotIdStr.substr(0, commaPos) : lotIdStr;
            
            if (!lotId.empty()) {
                string userLotQuery = "('" + userId + "','" + lotId + "','1000.000000')";
                dbClient.executeInsert("user_lot", userLotQuery);
            }
        }

        json response;
        response["key"] = userKey;
        return makeHttpResponse(201, response.dump(4));
    }
    catch (const exception& e) {
        json error = {{"error", "Internal server error"}, {"message", e.what()}};
        return makeHttpResponse(500, error.dump(4));
    }
}

string handleGetBalance(DatabaseManager& dbManager, const string& userKey, DatabaseClient& dbClient) {
    try {
        cout << "[INFO] Пользователь " << userKey << " запрашивает баланс" << endl;
        if (userKey.empty()) {
            json error = {{"error", "Нет заголовка X-USER-KEY"}};
            return makeHttpResponse(401, error.dump(4));
        }

        string userId = getUserIdByKey(userKey, dbClient);
        if (userId.empty()) {
            json error = {{"error", "Некорректный ключ пользователя"}};
            return makeHttpResponse(403, error.dump(4));
        }

        Vector<string> columns = {"user_lot.lot_id", "user_lot.quantity"};
        Vector<string> tables = {"user_lot"};
        Vector<Condition> conditions;
        conditions.push_back(Condition{"user_lot.user_id", userId, ""});
        
        Vector<string> results = dbClient.executeSelect(columns, tables, conditions);

        json response = json::array();
        for (size_t i = 0; i < results.get_size(); i++) {
            Vector<string> fields = parseCSVLine(results[i]);
            if (fields.get_size() >= 2) {
                json balance;
                balance["lot_id"] = safe_stoi(fields[0]);
                balance["quantity"] = safe_stod(fields[1]);
                response.push_back(balance);
            }
        }
        return makeHttpResponse(200, response.dump(4));
    }
    catch (const exception& e) {
        json error = {{"error", "Internal server error"}, {"message", e.what()}};
        return makeHttpResponse(500, error.dump(4));
    }
}

string handleGetOrders(DatabaseManager& dbManager, DatabaseClient& dbClient) {
    try {
        cout << "[INFO] Запрос списка ордеров" << endl;
        
        Vector<string> selectCol = {
            "order.order_id", 
            "order.user_id", 
            "order.pair_id", 
            "order.quantity", 
            "order.price", 
            "order.type", 
            "order.closed"
        };
        Vector<string> tables = {"order"};
        
        Vector<string> results = dbClient.executeSelect(selectCol, tables);
        
        json response = json::array();
        
        for (size_t i = 0; i < results.get_size(); i++) {
            Vector<string> fields = parseCSVLine(results[i]);
            
            if (fields.get_size() >= 6) {
                json order;
                order["order_id"] = safe_stoi(fields[0]);
                order["user_id"] = safe_stoi(fields[1]);
                order["pair_id"] = safe_stoi(fields[2]);
                order["quantity"] = safe_stod(fields[3]);
                order["price"] = safe_stod(fields[4]);
                order["type"] = fields[5];
                order["closed"] = fields[6];
                response.push_back(order);
            } else {
                cout << "[INFO] Некорректный формат строки ордера: " << results[i] << endl;
            }
        }
        
        return makeHttpResponse(200, response.dump(4));
        
    } catch (const exception& e) {
        json error = {{"error", "Internal server error"}, {"message", e.what()}};
        return makeHttpResponse(500, error.dump(4));
    }
}

PairInfo getPairInfo(const string& pairId, DatabaseClient& dbClient) {
    PairInfo info;
    
    Vector<string> columns = {"pair.first_lot_id", "pair.second_lot_id"};
    Vector<string> tables = {"pair"};
    Vector<Condition> conditions;
    conditions.push_back(Condition{"pair.pair_id", pairId, ""});
    
    Vector<string> results = dbClient.executeSelect(columns, tables, conditions);
    
    if (results.get_size() > 0 && !results[0].empty()) {
        Vector<string> fields = parseCSVLine(results[0]);
        if (fields.get_size() >= 2) {
            info.firstLotId = fields[0];
            info.secondLotId = fields[1];
            info.pairId = pairId;
        }
    }
    return info;
}

double getUserBalance(const string& userId, const string& lotId, DatabaseClient& dbClient) {
    Vector<string> columns = {"user_lot.quantity"};
    Vector<string> tables = {"user_lot"};
    Vector<Condition> conditions;
    conditions.push_back(Condition{"user_lot.user_id", userId, ""});
    conditions.push_back(Condition{"user_lot.lot_id", lotId, ""});
    
    Vector<string> results = dbClient.executeSelect(columns, tables, conditions);
    
    if (results.get_size() > 0 && !results[0].empty()) {
        string result = results[0];
        size_t commaPos = result.find(',');
        if (commaPos != string::npos) {
            result = result.substr(0, commaPos);
        }
        return safe_stod(result);
    }
    return 0.0;
}

void updateUserBalance(const string& userId, const string& lotId, double delta, DatabaseClient& dbClient) {
    try {
        if (abs(delta) < EPSILON) {
            return;
        }

        double currentBalance = getUserBalance(userId, lotId, dbClient);
        double newBalance = currentBalance + delta;
        
        if (newBalance < -EPSILON) {
            throw runtime_error("Отрицательный баланс у пользователя");
        }
        
        Vector<string> columns = {"user_lot.lot_id", "user_lot.quantity"};
        Vector<string> tables = {"user_lot"};
        Vector<Condition> conditions;
        conditions.push_back(Condition{"user_lot.user_id", userId, ""});
        
        Vector<string> allBalances = dbClient.executeSelect(columns, tables, conditions);
        
        Vector<Condition> deleteConditions;
        deleteConditions.push_back(Condition{"user_id", userId, ""});
        dbClient.executeDelete("user_lot", deleteConditions);
        
        for (size_t i = 0; i < allBalances.get_size(); i++) {
            Vector<string> fields = parseCSVLine(allBalances[i]);
            if (fields.get_size() >= 2) {
                string currentLotId = fields[0];
                string balanceStr = fields[1];
                
                double balance = safe_stod(balanceStr);
                
                if (currentLotId == lotId) {
                    balance = newBalance;
                }
                
                if (balance > EPSILON) {
                    string query = "('" + userId + "','" + currentLotId + "','" + to_string(balance) + "')";
                    dbClient.executeInsert("user_lot", query);
                }
            }
        }
        
        bool lotFound = false;
        for (size_t i = 0; i < allBalances.get_size(); i++) {
            Vector<string> fields = parseCSVLine(allBalances[i]);
            if (fields.get_size() >= 1 && fields[0] == lotId) {
                lotFound = true;
                break;
            }
        }
        
        if (!lotFound && newBalance > EPSILON) {
            string query = "('" + userId + "','" + lotId + "','" + to_string(newBalance) + "')";
            dbClient.executeInsert("user_lot", query);
        }
        
    } catch (const exception& e) {
        throw runtime_error("Ошибка обновления баланса: " + string(e.what()));
    }
}

string getCurrentTimestamp() {
    auto now = chrono::system_clock::now();
    auto now_time_t = chrono::system_clock::to_time_t(now);
    stringstream ss;
    ss << now_time_t;
    return ss.str();
}

bool updateOrderQuantity(const string& orderId, double newQuantity, DatabaseClient& dbClient) {
    try {
        Vector<string> columns = {"order.user_id", "order.pair_id", "order.price", "order.type", "order.closed"};
        Vector<string> tables = {"order"};
        Vector<Condition> conditions;
        conditions.push_back(Condition{"order.order_id", orderId, ""});
        
        Vector<string> orderInfo = dbClient.executeSelect(columns, tables, conditions);
        
        if (orderInfo.get_size() == 0) {
            return false;
        }
        
        Vector<string> fields = parseCSVLine(orderInfo[0]);
        if (fields.get_size() < 5) {
            return false;
        }
        
        string userId = fields[0];
        string pairId = fields[1];
        string price = fields[2];
        string type = fields[3];
        string closed = fields[4];
        
        Vector<Condition> deleteConditions;
        deleteConditions.push_back(Condition{"order_id", orderId, ""});
        dbClient.executeDelete("order", deleteConditions);
        
        string query = "('" + userId + "','" + pairId + "','" + to_string(newQuantity) + 
                      "','" + price + "','" + type + "','" + closed + "')";
        
        return dbClient.executeInsert("order", query);
        
    } catch (const exception& e) {
        cerr << "Ошибка обновления количества ордера: " << e.what() << endl;
        return false;
    }
}

bool closeOrderWithTimestamp(const string& orderId, const string& timestamp, DatabaseClient& dbClient) {
    try {
        string closeTime = timestamp.empty() ? getCurrentTimestamp() : timestamp;
        
        Vector<string> columns = {"order.user_id", "order.pair_id", "order.quantity", "order.price", "order.type"};
        Vector<string> tables = {"order"};
        Vector<Condition> conditions;
        conditions.push_back(Condition{"order.order_id", orderId, ""});
        
        Vector<string> orderInfo = dbClient.executeSelect(columns, tables, conditions);
        
        if (orderInfo.get_size() == 0) {
            return false;
        }
        
        Vector<string> fields = parseCSVLine(orderInfo[0]);
        if (fields.get_size() < 5) {
            return false;
        }
        
        string userId = fields[0];
        string pairId = fields[1];
        string quantity = fields[2];
        string price = fields[3];
        string type = fields[4];
        
        Vector<Condition> deleteConditions;
        deleteConditions.push_back(Condition{"order_id", orderId, ""});
        dbClient.executeDelete("order", deleteConditions);
        
        string query = "('" + userId + "','" + pairId + "','" + quantity + 
                      "','" + price + "','" + type + "','" + closeTime + "')";
        
        return dbClient.executeInsert("order", query);
        
    } catch (const exception& e) {
        cerr << "Ошибка закрытия ордера: " << e.what() << endl;
        return false;
    }
}

void unlockFundsForOrder(const string& userId, const string& orderType, 
                        const string& assetLot, const string& currencyLot, 
                        double quantity, double price, DatabaseClient& dbClient) {
    string lotToUnlock;
    double amountToUnlock;
    
    if (orderType == "buy") {
        lotToUnlock = currencyLot;
        amountToUnlock = quantity * price;
    }
    else {
        lotToUnlock = assetLot;
        amountToUnlock = quantity;
    }
    
    updateUserBalance(userId, lotToUnlock, +amountToUnlock, dbClient);
}

string handleCreateOrder(DatabaseManager& dbManager, const string& body, const string& userKey, DatabaseClient& dbClient) {
    try {
        cout << "[INFO] Пользователь " << userKey << " создаёт ордер: " << body << endl;
        
        if (userKey.empty()) {
            return makeHttpResponse(401, R"({"error": "Отсутствует заголовок X-USER-KEY"})");
        }
        
        string userId = getUserIdByKey(userKey, dbClient);
        if (userId.empty()) {
            return makeHttpResponse(403, R"({"error": "Неверный ключ пользователя"})");
        }
        
        json request = parseJsonBody(body);
        if (!hasField(request, "pair_id") || !hasField(request, "quantity") || 
            !hasField(request, "price") || !hasField(request, "type")) {
            return makeHttpResponse(400, R"({"error": "Отсутствуют обязательные поля"})");
        }
        
        int pairIdInt = request["pair_id"].get<int>();
        string pairId = to_string(pairIdInt);
        double originalQuantity = request["quantity"].get<double>();
        double ourPrice = request["price"].get<double>();
        string orderType = request["type"].get<string>();
        
        if (orderType != "buy" && orderType != "sell") {
            return makeHttpResponse(400, R"({"error": "тип ордера только 'buy' или 'sell'"})");
        }
        
        if (originalQuantity <= 0 || ourPrice <= 0) {
            return makeHttpResponse(400, R"({"error": "запрос и цена должны быть положительными"})");
        }
        
        PairInfo pair = getPairInfo(pairId, dbClient);
        if (pair.firstLotId.empty() || pair.secondLotId.empty()) {
            return makeHttpResponse(404, R"({"error": "Пара не найдена"})");
        }
        
        string assetLot = pair.firstLotId;
        string currencyLot = pair.secondLotId;
        
        if (orderType == "buy") {
            double lockedAmount = originalQuantity * ourPrice;
            double currentBalance = getUserBalance(userId, currencyLot, dbClient);
            if (currentBalance < lockedAmount - EPSILON) {
                json error = {{"error", "Недостаточно средств"}, {"запрошено", lockedAmount}, {"доступно", currentBalance}};
                return makeHttpResponse(400, error.dump(4));
            }
            
            updateUserBalance(userId, currencyLot, -lockedAmount, dbClient);
        } else if (orderType == "sell") {
            double currentBalance = getUserBalance(userId, assetLot, dbClient);
            if (currentBalance < originalQuantity - EPSILON) {
                json error = {{"error", "Недостаточно средств"}, {"запрошено", originalQuantity}, {"доступно", currentBalance}};
                return makeHttpResponse(400, error.dump(4));
            }
            
            updateUserBalance(userId, assetLot, -originalQuantity, dbClient);
        }
        
        string oppositeType = (orderType == "buy") ? "sell" : "buy";
        
        Vector<string> columns = {
            "order.order_id", 
            "order.user_id", 
            "order.quantity", 
            "order.price", 
            "order.type",
            "order.closed"
        };
        Vector<string> tables = {"order"};
        Vector<Condition> conditions;
        conditions.push_back(Condition{"order.pair_id", pairId, ""});
        conditions.push_back(Condition{"order.type", oppositeType, ""});
        conditions.push_back(Condition{"order.closed", "", ""});
        
        Vector<string> results = dbClient.executeSelect(columns, tables, conditions);
        
        Vector<string> matchingOrderIds;
        Vector<string> matchingUserIds;
        Vector<double> matchingQuantities;
        Vector<double> matchingPrices;
        
        for (size_t i = 0; i < results.get_size(); i++) {
            Vector<string> fields = parseCSVLine(results[i]);
            if (fields.get_size() < 6) continue;
            
            string orderId = fields[0];
            string matchUserId = fields[1];
            string qtyStr = fields[2];
            string priceStr = fields[3];
            string type = fields[4];
            string closed = fields[5];
            
            if (!isStringEmptyOrWhitespace(closed)) {
                continue;
            }
            
            if (matchUserId == userId) {
                continue;
            }
            
            double matchQuantity = safe_stod(qtyStr);
            double matchPrice = safe_stod(priceStr);
            
            bool priceMatch = false;
            if (orderType == "buy") {
                priceMatch = (matchPrice <= ourPrice + EPSILON);
            } else {
                priceMatch = (matchPrice >= ourPrice - EPSILON);
            }
            
            if (priceMatch && matchQuantity > EPSILON) {
                matchingOrderIds.push_back(orderId);
                matchingUserIds.push_back(matchUserId);
                matchingQuantities.push_back(matchQuantity);
                matchingPrices.push_back(matchPrice);
            }
        }
        
        if (orderType == "buy") {
            for (size_t i = 0; i < matchingOrderIds.get_size(); i++) {
                for (size_t j = i + 1; j < matchingOrderIds.get_size(); j++) {
                    if (matchingPrices[j] < matchingPrices[i]) {
                        swap(matchingOrderIds[i], matchingOrderIds[j]);
                        swap(matchingUserIds[i], matchingUserIds[j]);
                        swap(matchingQuantities[i], matchingQuantities[j]);
                        swap(matchingPrices[i], matchingPrices[j]);
                    }
                }
            }
        }
        
        double remainingQuantity = originalQuantity;
        double executedQuantity = 0;
        double totalExecutedValue = 0;
        bool anyTradeExecuted = false;
        
        for (size_t i = 0; i < matchingOrderIds.get_size() && remainingQuantity > EPSILON; i++) {
            string matchOrderId = matchingOrderIds[i];
            string matchUserId = matchingUserIds[i];
            double matchQuantity = matchingQuantities[i];
            double executionPrice = (orderType == "buy") ? matchingPrices[i] : ourPrice;
            
            double tradeQuantity = min(remainingQuantity, matchQuantity);
            double tradeValue = tradeQuantity * executionPrice;
            
            totalExecutedValue += tradeValue;
            anyTradeExecuted = true;
            
            if (orderType == "buy") {
                updateUserBalance(userId, assetLot, +tradeQuantity, dbClient);
                updateUserBalance(matchUserId, currencyLot, +tradeValue, dbClient);
                
                Vector<string> sellColumns = {"order.price"};
                Vector<string> sellTables = {"order"};
                Vector<Condition> sellConditions;
                sellConditions.push_back(Condition{"order.order_id", matchOrderId, ""});
                
                Vector<string> sellOrderResults = dbClient.executeSelect(sellColumns, sellTables, sellConditions);
                
                if (sellOrderResults.get_size() > 0) {
                    Vector<string> priceFields = parseCSVLine(sellOrderResults[0]);
                    if (!priceFields.empty()) {
                        double sellOrderPrice = safe_stod(priceFields[0]);
                        if (sellOrderPrice < executionPrice + EPSILON) {
                            double excessPerUnit = executionPrice - sellOrderPrice;
                            double totalExcess = tradeQuantity * excessPerUnit;
                            updateUserBalance(userId, currencyLot, +totalExcess, dbClient);
                        }
                    }
                }
            }
            else if (orderType == "sell") {
                updateUserBalance(userId, currencyLot, +tradeValue, dbClient);
                updateUserBalance(matchUserId, assetLot, +tradeQuantity, dbClient);
                
                Vector<string> buyColumns = {"order.price"};
                Vector<string> buyTables = {"order"};
                Vector<Condition> buyConditions;
                buyConditions.push_back(Condition{"order.order_id", matchOrderId, ""});
                
                Vector<string> buyOrderResults = dbClient.executeSelect(buyColumns, buyTables, buyConditions);
                
                if (buyOrderResults.get_size() > 0) {
                    Vector<string> priceFields = parseCSVLine(buyOrderResults[0]);
                    if (!priceFields.empty()) {
                        double buyOrderPrice = safe_stod(priceFields[0]);
                        if (buyOrderPrice > executionPrice + EPSILON) {
                            double excessPerUnit = buyOrderPrice - executionPrice;
                            double totalExcess = tradeQuantity * excessPerUnit;
                            updateUserBalance(matchUserId, currencyLot, +totalExcess, dbClient);
                        }
                    }
                }
            }
            
            double newMatchQuantity = matchQuantity - tradeQuantity;
            
            if (newMatchQuantity <= EPSILON) {
                closeOrderWithTimestamp(matchOrderId, "", dbClient);
            } 
            else {
                updateOrderQuantity(matchOrderId, newMatchQuantity, dbClient);
                
                string closedField = getCurrentTimestamp();
                string orderQuery = "('" + matchUserId + "','" + pairId + "','" + 
                                  to_string(tradeQuantity) + "','" + to_string(executionPrice) + 
                                  "','" + oppositeType + "','" + closedField + "')";
                dbClient.executeInsert("order", orderQuery);
            }
            
            remainingQuantity -= tradeQuantity;
            executedQuantity += tradeQuantity;
        }
        
        if (anyTradeExecuted && orderType == "buy") {
            double initiallyLocked = originalQuantity * ourPrice;
            double actuallySpent = totalExecutedValue;
            double amountToReturn = initiallyLocked - actuallySpent - (remainingQuantity * ourPrice);
            
            if (amountToReturn > EPSILON) {
                updateUserBalance(userId, currencyLot, +amountToReturn, dbClient);
            }
        }
        
        int responseId = dbClient.getMaxOrderId() + 1;
        
        if (executedQuantity > EPSILON && remainingQuantity > EPSILON) {
            double avgExecutionPrice = totalExecutedValue / executedQuantity;
            
            string closedField = getCurrentTimestamp();
            string orderQuery = "('" + userId + "','" + pairId + "','" + 
                              to_string(executedQuantity) + "','" + to_string(avgExecutionPrice) + 
                              "','" + orderType + "','" + closedField + "')";
            dbClient.executeInsert("order", orderQuery);
            
            closedField = "";
            orderQuery = "('" + userId + "','" + pairId + "','" + 
                        to_string(remainingQuantity) + "','" + to_string(ourPrice) + 
                        "','" + orderType + "','" + closedField + "')";
            dbClient.executeInsert("order", orderQuery);
                 
        } else if (executedQuantity > EPSILON) {
            double avgExecutionPrice = totalExecutedValue / executedQuantity;
            string closedField = getCurrentTimestamp();
            string orderQuery = "('" + userId + "','" + pairId + "','" + 
                              to_string(executedQuantity) + "','" + to_string(avgExecutionPrice) + 
                              "','" + orderType + "','" + closedField + "')";
            dbClient.executeInsert("order", orderQuery);
                 
        } else {
            string closedField = "";
            string orderQuery = "('" + userId + "','" + pairId + "','" + 
                              to_string(originalQuantity) + "','" + to_string(ourPrice) + 
                              "','" + orderType + "','" + closedField + "')";
            dbClient.executeInsert("order", orderQuery);
        }
        
        responseId = dbClient.getMaxOrderId();
        
        json response;
        response["order_id"] = responseId;
        return makeHttpResponse(201, response.dump(4));
        
    } catch (const exception& e) {
        json error = {{"error", "Internal server error"}, {"message", e.what()}};
        return makeHttpResponse(500, error.dump(4));
    }
}

string handleDeleteOrder(DatabaseManager& dbManager, const string& body, const string& userKey, DatabaseClient& dbClient) {
    try {
        cout << "[INFO] Пользователь " << userKey << " удаляет ордер: " << body << endl;
        
        if (userKey.empty()) {
            return makeHttpResponse(401, R"({"error": "Нет заголовка X-USER-KEY"})");
        }
        
        string userId = getUserIdByKey(userKey, dbClient);
        if (userId.empty()) {
            return makeHttpResponse(403, R"({"error": "Неверный ключ пользователя"})");
        }
        
        json request = parseJsonBody(body);
        if (!hasField(request, "order_id")) {
            return makeHttpResponse(400, R"({"error": "Отсутствует order_id"})");
        }
        
        string orderId = to_string(request["order_id"].get<int>());
        
        Vector<string> columns = {"order.user_id", "order.closed"};
        Vector<string> tables = {"order"};
        Vector<Condition> conditions;
        conditions.push_back(Condition{"order.order_id", orderId, ""});
        
        Vector<string> results = dbClient.executeSelect(columns, tables, conditions);
        
        if (results.get_size() == 0) {
            return makeHttpResponse(404, R"({"error": "Ордер не найден"})");
        }
        
        Vector<string> fields = parseCSVLine(results[0]);
        if (fields.get_size() < 2) {
            return makeHttpResponse(404, R"({"error": "Ордер не найден"})");
        }
        
        string orderUserId = fields[0];
        string closed = fields[1];
        
        if (orderUserId != userId) {
            return makeHttpResponse(403, R"({"error": "Ордер не принадлежит пользователю"})");
        }
        
        if (!isStringEmptyOrWhitespace(closed)) {
            return makeHttpResponse(403, R"({"error": "Нельзя удалить закрытый ордер"})");
        }
        
        Vector<string> orderColumns = {
            "order.user_id", 
            "order.pair_id", 
            "order.quantity", 
            "order.price", 
            "order.type"
        };
        Vector<Condition> orderConditions;
        orderConditions.push_back(Condition{"order.order_id", orderId, ""});
        
        Vector<string> orderResults = dbClient.executeSelect(orderColumns, tables, orderConditions);
        
        if (orderResults.get_size() == 0) {
            return makeHttpResponse(404, R"({"error": "Ордер не найден"})");
        }
        
        Vector<string> orderFields = parseCSVLine(orderResults[0]);
        if (orderFields.get_size() < 5) {
            return makeHttpResponse(404, R"({"error": "Ордер не найден"})");
        }
        
        string orderUserId2 = orderFields[0];
        string pairId = orderFields[1];
        string orderQuantity = orderFields[2];
        string orderPrice = orderFields[3];
        string orderType = orderFields[4];
        
        double quantity = safe_stod(orderQuantity);
        double price = safe_stod(orderPrice);
        
        PairInfo pair = getPairInfo(pairId, dbClient);
        if (!pair.firstLotId.empty() && !pair.secondLotId.empty()) {
            string assetLot = pair.firstLotId;
            string currencyLot = pair.secondLotId;
            
            unlockFundsForOrder(userId, orderType, assetLot, currencyLot, quantity, price, dbClient);
        }
        
        closeOrderWithTimestamp(orderId, "", dbClient);
        
        json response;
        response["order_id"] = safe_stoi(orderId);
        return makeHttpResponse(200, response.dump(4));
        
    } catch (const exception& e) {
        json error = {{"error", "Internal server error"}, {"message", e.what()}};
        return makeHttpResponse(500, error.dump(4));
    }
}