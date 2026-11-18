// ============================================================================
//  WIMS Web Backend
// ============================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <algorithm>
#include <map>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#define main wims_console_main
#define class struct
#define private public
#include "pbl.cpp"
#undef private
#undef class
#undef main

// ----------------------------------------------------------------------------
//  JSON helpers
// ----------------------------------------------------------------------------
static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += (char)c;
        }
    }
    return out;
}
static std::string jstr(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }
static std::string jnum(double d) {
    std::ostringstream o; o << std::fixed << std::setprecision(2) << d; return o.str();
}
static std::string jbool(bool b) { return b ? "true" : "false"; }

static std::string jsonGetString(const std::string& body, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t k = body.find(pat);
    if (k == std::string::npos) return "";
    size_t colon = body.find(':', k + pat.size());
    if (colon == std::string::npos) return "";
    size_t i = colon + 1;
    while (i < body.size() && std::isspace((unsigned char)body[i])) ++i;
    if (i >= body.size() || body[i] != '"') return "";
    ++i;
    std::string out;
    while (i < body.size() && body[i] != '"') {
        if (body[i] == '\\' && i + 1 < body.size()) {
            ++i; char c = body[i];
            if (c == 'n') out += '\n';
            else if (c == 't') out += '\t';
            else if (c == 'r') out += '\r';
            else out += c;
        } else out += body[i];
        ++i;
    }
    return out;
}
static double jsonGetNumber(const std::string& body, const std::string& key, double def) {
    std::string pat = "\"" + key + "\"";
    size_t k = body.find(pat);
    if (k == std::string::npos) return def;
    size_t colon = body.find(':', k + pat.size());
    if (colon == std::string::npos) return def;
    size_t i = colon + 1;
    while (i < body.size() && std::isspace((unsigned char)body[i])) ++i;
    const char* start = body.c_str() + i;
    char* end = NULL;
    double v = std::strtod(start, &end);
    if (end == start) return def;
    return v;
}

// ----------------------------------------------------------------------------
//  HTTP response container
// ----------------------------------------------------------------------------
struct Res {
    int status;
    std::string body;
    std::string ctype;
};
static Res ok(const std::string& data) {
    Res r; r.status = 200; r.ctype = "application/json";
    r.body = "{\"ok\":true,\"data\":" + data + "}"; return r;
}
static Res okMsg(const std::string& message) {
    Res r; r.status = 200; r.ctype = "application/json";
    r.body = "{\"ok\":true,\"message\":" + jstr(message) + "}"; return r;
}
static Res err(const std::string& message, int status = 400) {
    Res r; r.status = status; r.ctype = "application/json";
    r.body = "{\"ok\":false,\"error\":" + jstr(message) + "}"; return r;
}
static Res fileRes(const std::string& body, const std::string& ctype) {
    Res r; r.status = 200; r.ctype = ctype; r.body = body; return r;
}

// ----------------------------------------------------------------------------
//  Sales record + date helper
// ----------------------------------------------------------------------------
struct Sale {
    std::string orderId, productId, warehouseId, date;
    int quantity;
    double unitPrice, amount;
};
static std::string todayStr() {
    std::time_t t = std::time(NULL);
    std::tm* tm = std::localtime(&t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    return std::string(buf);
}

// ----------------------------------------------------------------------------
//  Service layer
// ----------------------------------------------------------------------------
struct Service {
    // --- multiple warehouses (each owns its own InventoryList) --------------
    std::vector<Warehouse*> warehouses;
    std::string activeId;

    Manager manager;
    OrderQueue queue;
    AuditStack audit;
    std::vector<Customer> customers;
    std::vector<Supplier> suppliers;
    std::vector<Order> history;
    std::vector<Sale> sales;
    std::map<std::string, std::string> orderWarehouse;
    int nextOrder = 1;

    // --- simple admin credentials ------------------------------------------
    std::string adminUser = "admin";
    std::string adminPass = "admin123";
    std::set<std::string> tokens;

    Service() { loadAll(); }
    ~Service() { for (size_t i = 0; i < warehouses.size(); ++i) delete warehouses[i]; }

    // ---- warehouse helpers --------------------------------------------------
    Warehouse* findWarehouse(const std::string& id) {
        for (size_t i = 0; i < warehouses.size(); ++i)
            if (warehouses[i]->id == id) return warehouses[i];
        return NULL;
    }
    Warehouse* active() {
        Warehouse* w = findWarehouse(activeId);
        if (w == NULL && !warehouses.empty()) { w = warehouses[0]; activeId = w->id; }
        return w;
    }
    std::string invFile(const std::string& id) { return "inventory_" + id + ".txt"; }

    std::vector<Product> productsOf(Warehouse* w) {
        std::vector<Product> out;
        if (w == NULL) return out;
        InventoryList::Node* t = w->inventory.head;
        while (t != NULL) { out.push_back(t->product); t = t->next; }
        return out;
    }
    std::vector<Order> pendingList() {
        std::vector<Order> out;
        OrderQueue::Node* t = queue.front;
        while (t != NULL) { out.push_back(t->order); t = t->next; }
        return out;
    }
    std::vector<std::string> auditList() {
        std::vector<std::string> out;
        AuditStack::Node* t = audit.top;
        while (t != NULL) { out.push_back(t->action); t = t->next; }
        return out;
    }

    // ---- persistence --------------------------------------------------------
    void loadInventory(Warehouse* w) {
        std::ifstream f(invFile(w->id).c_str());
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            try { w->inventory.add(Product::fromFile(line)); } catch (...) {}
        }
    }
    bool saveInventory(Warehouse* w) {
        std::ofstream f(invFile(w->id).c_str());
        if (!f) return false;
        InventoryList::Node* t = w->inventory.head;
        while (t != NULL) { f << t->product.toFile() << '\n'; t = t->next; }
        return f.good();
    }

    void loadAll() {
        std::string line;

        std::ifstream wf("warehouses.txt");
        while (std::getline(wf, line)) {
            if (line.empty()) continue;
            std::stringstream in(line);
            std::string id, loc, cap, act;
            std::getline(in, id, '\t');
            std::getline(in, loc, '\t');
            std::getline(in, cap, '\t');
            std::getline(in, act, '\t');
            if (id.empty()) continue;
            Warehouse* w = new Warehouse();
            w->id = id;
            w->location = loc.empty() ? "Warehouse" : loc;
            w->capacity = cap.empty() ? 1000 : std::atoi(cap.c_str());
            warehouses.push_back(w);
            if (act == "1") activeId = id;
        }
        if (warehouses.empty()) {
            Warehouse* w = new Warehouse();
            w->id = "WH-01"; w->location = "Main Warehouse"; w->capacity = 1000;
            warehouses.push_back(w);
            activeId = "WH-01";
        }
        if (activeId.empty()) activeId = warehouses[0]->id;
        for (size_t i = 0; i < warehouses.size(); ++i) loadInventory(warehouses[i]);

        std::ifstream cf("customers.txt");
        while (std::getline(cf, line)) {
            std::stringstream in(line); std::string i, n, p;
            if (std::getline(in, i, '\t') && std::getline(in, n, '\t') && std::getline(in, p, '\t'))
                customers.emplace_back(i, n, p);
        }
        std::ifstream sf("suppliers.txt");
        while (std::getline(sf, line)) {
            std::stringstream in(line); std::string i, n, p;
            if (std::getline(in, i, '\t') && std::getline(in, n, '\t') && std::getline(in, p, '\t'))
                suppliers.emplace_back(i, n, p);
        }
        std::ifstream ow("order_warehouse.txt");
        while (std::getline(ow, line)) {
            std::stringstream in(line); std::string oid, wid;
            if (std::getline(in, oid, '\t') && std::getline(in, wid, '\t')) orderWarehouse[oid] = wid;
        }
        std::ifstream of("orders.txt");
        while (std::getline(of, line)) {
            if (line.empty()) continue;
            try {
                Order o = Order::fromFile(line);
                history.push_back(o);
                if (o.status == "Pending") queue.enqueue(o);
                if (o.id.size() > 2) nextOrder = std::max(nextOrder, std::stoi(o.id.substr(2)) + 1);
            } catch (...) {}
        }
        std::ifstream sl("sales.txt");
        while (std::getline(sl, line)) {
            if (line.empty()) continue;
            std::stringstream in(line);
            Sale s;
            std::string qty, up, amt;
            if (!std::getline(in, s.orderId, '\t')) continue;
            std::getline(in, s.productId, '\t');
            std::getline(in, s.warehouseId, '\t');
            std::getline(in, qty, '\t');
            std::getline(in, up, '\t');
            std::getline(in, amt, '\t');
            std::getline(in, s.date, '\t');
            s.quantity  = std::atoi(qty.c_str());
            s.unitPrice = std::atof(up.c_str());
            s.amount    = std::atof(amt.c_str());
            sales.push_back(s);
        }
        audit.load();
    }

    bool saveAll() {
        bool good = true;
        std::ofstream wf("warehouses.txt");
        if (!wf) good = false;
        else {
            for (size_t i = 0; i < warehouses.size(); ++i)
                wf << warehouses[i]->id << '\t' << warehouses[i]->location << '\t'
                   << warehouses[i]->capacity << '\t' << (warehouses[i]->id == activeId ? 1 : 0) << '\n';
            if (!wf.good()) good = false;
        }
        for (size_t i = 0; i < warehouses.size(); ++i)
            if (!saveInventory(warehouses[i])) good = false;

        std::ofstream orders("orders.txt");
        if (!orders) good = false;
        else { for (size_t i = 0; i < history.size(); ++i) orders << history[i].toFile() << '\n'; if (!orders.good()) good = false; }

        std::ofstream ows("order_warehouse.txt");
        for (std::map<std::string, std::string>::iterator it = orderWarehouse.begin(); it != orderWarehouse.end(); ++it)
            ows << it->first << '\t' << it->second << '\n';

        std::ofstream sal("sales.txt");
        for (size_t i = 0; i < sales.size(); ++i)
            sal << sales[i].orderId << '\t' << sales[i].productId << '\t'
                << sales[i].warehouseId << '\t' << sales[i].quantity << '\t'
                << std::fixed << std::setprecision(2) << sales[i].unitPrice << '\t'
                << sales[i].amount << '\t' << sales[i].date << '\n';

        if (!audit.save()) good = false;
        std::ofstream c("customers.txt");
        for (size_t i = 0; i < customers.size(); ++i) c << customers[i].toFile() << '\n';
        std::ofstream s("suppliers.txt");
        for (size_t i = 0; i < suppliers.size(); ++i) s << suppliers[i].toFile() << '\n';
        return good;
    }

    // ---- JSON builders ------------------------------------------------------
    std::string productJson(const Product& p) {
        std::ostringstream o;
        o << "{\"id\":" << jstr(p.id) << ",\"name\":" << jstr(p.name)
          << ",\"category\":" << jstr(p.category) << ",\"price\":" << jnum(p.price)
          << ",\"quantity\":" << p.quantity << ",\"reorderLevel\":" << p.reorderLevel
          << ",\"lowStock\":" << jbool(p.quantity <= p.reorderLevel) << "}";
        return o.str();
    }
    std::string warehouseJson(Warehouse* w) {
        std::ostringstream o;
        o << "{\"id\":" << jstr(w->id) << ",\"location\":" << jstr(w->location)
          << ",\"capacity\":" << w->capacity
          << ",\"stock\":" << w->inventory.totalQuantity()
          << ",\"products\":" << productsOf(w).size()
          << ",\"active\":" << jbool(w->id == activeId) << "}";
        return o.str();
    }
    std::string orderJson(const Order& o) {
        std::string wid = orderWarehouse.count(o.id) ? orderWarehouse[o.id] : activeId;
        Warehouse* w = findWarehouse(wid);
        int avail = 0;
        double unitPrice = 0.0;
        if (w != NULL) { Product* p = w->inventory.find(o.productId); if (p != NULL) { avail = p->quantity; unitPrice = p->price; } }
        double amount = unitPrice * o.quantity;
        for (size_t i = 0; i < sales.size(); ++i)
            if (sales[i].orderId == o.id) { unitPrice = sales[i].unitPrice; amount = sales[i].amount; break; }
        std::ostringstream s;
        s << "{\"id\":" << jstr(o.id) << ",\"customerId\":" << jstr(o.customerId)
          << ",\"productId\":" << jstr(o.productId) << ",\"quantity\":" << o.quantity
          << ",\"status\":" << jstr(o.status) << ",\"warehouseId\":" << jstr(wid)
          << ",\"warehouseName\":" << jstr(w ? w->location : "")
          << ",\"available\":" << avail
          << ",\"unitPrice\":" << jnum(unitPrice)
          << ",\"amount\":" << jnum(amount) << "}";
        return s.str();
    }
    std::string customerJson(const Customer& c) {
        return "{\"id\":" + jstr(c.id) + ",\"name\":" + jstr(c.name) + ",\"phone\":" + jstr(c.phone) + "}";
    }
    std::string supplierJson(const Supplier& s) {
        return "{\"id\":" + jstr(s.id) + ",\"name\":" + jstr(s.name) + ",\"phone\":" + jstr(s.phone) + "}";
    }

    // ---- endpoints ----------------------------------------------------------
    Res stateJson() {
        Warehouse* w = active();
        std::vector<Product> ps = productsOf(w);
        int low = 0;
        for (size_t i = 0; i < ps.size(); ++i) if (ps[i].quantity <= ps[i].reorderLevel) ++low;

        std::string today = todayStr();
        double totalSales = 0.0, salesToday = 0.0, todayOrders = 0.0;
        for (size_t i = 0; i < sales.size(); ++i) {
            totalSales += sales[i].amount;
            if (sales[i].date == today) { salesToday += sales[i].amount; todayOrders += 1; }
        }
        double stockValue = 0.0;
        for (size_t i = 0; i < ps.size(); ++i) stockValue += ps[i].price * ps[i].quantity;
        double stockValueAll = 0.0;
        for (size_t i = 0; i < warehouses.size(); ++i) {
            std::vector<Product> wp = productsOf(warehouses[i]);
            for (size_t k = 0; k < wp.size(); ++k) stockValueAll += wp[k].price * wp[k].quantity;
        }

        std::ostringstream o;
        o << "{\"manager\":{\"id\":" << jstr(manager.id) << ",\"name\":" << jstr(manager.name)
          << ",\"role\":" << jstr(manager.role) << ",\"describe\":" << jstr(manager.describe()) << "},";
        o << "\"active\":" << jstr(w ? w->id : "") << ",";
        o << "\"warehouse\":{";
        if (w) o << "\"id\":" << jstr(w->id) << ",\"location\":" << jstr(w->location)
                 << ",\"capacity\":" << w->capacity << ",\"stock\":" << w->inventory.totalQuantity();
        else  o << "\"id\":\"\",\"location\":\"\",\"capacity\":0,\"stock\":0";
        o << "},";
        o << "\"warehouses\":[";
        for (size_t i = 0; i < warehouses.size(); ++i) { if (i) o << ","; o << warehouseJson(warehouses[i]); }
        o << "],";
        o << "\"totals\":{"
          << "\"products\":" << ps.size()
          << ",\"stock\":" << (w ? w->inventory.totalQuantity() : 0)
          << ",\"lowStock\":" << low
          << ",\"pending\":" << pendingList().size()
          << ",\"customers\":" << customers.size()
          << ",\"suppliers\":" << suppliers.size()
          << ",\"orders\":" << history.size()
          << ",\"warehouses\":" << warehouses.size()
          << ",\"audit\":" << auditList().size()
          << ",\"salesCount\":" << sales.size()
          << ",\"salesTodayCount\":" << (long long)todayOrders
          << ",\"totalSales\":" << jnum(totalSales)
          << ",\"salesToday\":" << jnum(salesToday)
          << ",\"stockValue\":" << jnum(stockValue)
          << ",\"stockValueAll\":" << jnum(stockValueAll) << "}}";
        return ok(o.str());
    }

    Res warehousesJson() {
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < warehouses.size(); ++i) { if (i) o << ","; o << warehouseJson(warehouses[i]); }
        o << "]";
        return ok(o.str());
    }

    Res productsJson() {
        std::vector<Product> ps = productsOf(active());
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < ps.size(); ++i) { if (i) o << ","; o << productJson(ps[i]); }
        o << "]";
        return ok(o.str());
    }

    Res lowStockJson() {
        std::vector<Product> ps = productsOf(active());
        std::ostringstream o; o << "[";
        bool first = true;
        for (size_t i = 0; i < ps.size(); ++i)
            if (ps[i].quantity <= ps[i].reorderLevel) { if (!first) o << ","; first = false; o << productJson(ps[i]); }
        o << "]";
        return ok(o.str());
    }

    Res ordersJson() {
        std::vector<Order> pend = pendingList();
        std::ostringstream o;
        o << "{\"pending\":[";
        for (size_t i = 0; i < pend.size(); ++i) { if (i) o << ","; o << orderJson(pend[i]); }
        o << "],\"history\":[";
        for (size_t i = 0; i < history.size(); ++i) { if (i) o << ","; o << orderJson(history[i]); }
        o << "]}";
        return ok(o.str());
    }

    Res customersJson() {
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < customers.size(); ++i) { if (i) o << ","; o << customerJson(customers[i]); }
        o << "]";
        return ok(o.str());
    }
    Res suppliersJson() {
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < suppliers.size(); ++i) { if (i) o << ","; o << supplierJson(suppliers[i]); }
        o << "]";
        return ok(o.str());
    }
    Res auditJson() {
        std::vector<std::string> a = auditList();
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < a.size(); ++i) { if (i) o << ","; o << jstr(a[i]); }
        o << "]";
        return ok(o.str());
    }

    // ---- auth ---------------------------------------------------------------
    Res login(const std::string& u, const std::string& p) {
        if (u == adminUser && p == adminPass) {
            std::ostringstream t;
            t << "tok" << (std::rand() ^ (int)std::time(NULL)) << (std::rand());
            tokens.insert(t.str());
            std::string data = "{\"token\":" + jstr(t.str())
                             + ",\"user\":" + jstr(manager.name)
                             + ",\"role\":" + jstr(manager.role) + "}";
            return ok(data);
        }
        return err("Invalid username or password.", 401);
    }
    bool checkToken(const std::string& t) { return !t.empty() && tokens.count(t) > 0; }
    void logout(const std::string& t) { tokens.erase(t); }

    // ---- product mutations (operate on the ACTIVE warehouse) ---------------
    Res addProduct(std::string id, std::string name, std::string cat, double price, int qty, int reorder) {
        Warehouse* w = active();
        if (w == NULL) return err("No warehouse selected.");
        if (id.empty() || name.empty() || cat.empty() || price < 0 || qty < 0 || reorder < 0)
            return err("Invalid product details.");
        if (w->inventory.totalQuantity() + qty > w->capacity) return err("Warehouse capacity exceeded.");
        if (!w->inventory.add(Product(id, name, cat, price, qty, reorder))) return err("Product ID already exists.");
        audit.push("Added product " + id + " to " + w->id);
        saveAll();
        return okMsg("Product added.");
    }
    Res restock(std::string id, int amount) {
        Warehouse* w = active();
        if (w == NULL) return err("No warehouse selected.");
        Product* p = w->inventory.find(id);
        if (p == NULL) return err("Product not found.", 404);
        if (amount <= 0) return err("Quantity must be positive.");
        if (w->inventory.totalQuantity() + amount > w->capacity) return err("Warehouse capacity exceeded.");
        p->addStock(amount);
        audit.push("Restocked " + id + " in " + w->id);
        saveAll();
        return okMsg("Stock updated.");
    }
    Res changePrice(std::string id, double price) {
        Warehouse* w = active();
        Product* p = w ? w->inventory.find(id) : NULL;
        if (p == NULL) return err("Product not found.", 404);
        if (price < 0) return err("Price cannot be negative.");
        p->price = price;
        audit.push("Changed price of " + id);
        saveAll();
        return okMsg("Price updated.");
    }
    Res removeProduct(std::string id) {
        if (!manager.canManage()) return err("Manager access required.", 403);
        Warehouse* w = active();
        if (w != NULL && w->inventory.remove(id)) {
            audit.push("Removed product " + id + " from " + w->id);
            saveAll();
            return okMsg("Product removed.");
        }
        return err("Product not found.", 404);
    }

    // ---- people -------------------------------------------------------------
    bool customerExists(const std::string& id) {
        for (size_t i = 0; i < customers.size(); ++i) if (customers[i].id == id) return true;
        return false;
    }
    Res addCustomer(std::string id, std::string name, std::string phone) {
        if (id.empty() || name.empty() || phone.empty() || customerExists(id))
            return err("Invalid details or customer ID already exists.");
        customers.emplace_back(id, name, phone);
        audit.push("Added customer " + id);
        saveAll();
        return okMsg("Customer added.");
    }
    Res addSupplier(std::string id, std::string name, std::string phone) {
        if (id.empty() || name.empty() || phone.empty()) return err("All fields are required.");
        for (size_t i = 0; i < suppliers.size(); ++i)
            if (suppliers[i].id == id) return err("Supplier ID already exists.");
        suppliers.emplace_back(id, name, phone);
        audit.push("Added supplier " + id);
        saveAll();
        return okMsg("Supplier added.");
    }

    // ---- warehouses ---------------------------------------------------------
    Res addWarehouse(std::string id, std::string location, int capacity) {
        if (id.empty() || location.empty() || capacity <= 0) return err("Enter a valid ID, location and capacity.");
        if (findWarehouse(id) != NULL) return err("Warehouse ID already exists.");
        Warehouse* w = new Warehouse();
        w->id = id; w->location = location; w->capacity = capacity;
        warehouses.push_back(w);
        audit.push("Added warehouse " + id);
        saveAll();
        return okMsg("Warehouse added.");
    }
    Res setActiveWarehouse(std::string id) {
        if (findWarehouse(id) == NULL) return err("Warehouse not found.", 404);
        activeId = id;
        saveAll();
        return okMsg("Switched to " + id + ".");
    }
    Res removeWarehouse(std::string id) {
        if (warehouses.size() <= 1) return err("You must keep at least one warehouse.");
        Warehouse* w = findWarehouse(id);
        if (w == NULL) return err("Warehouse not found.", 404);
        if (!productsOf(w).empty()) return err("Move or delete its products first (warehouse is not empty).");
        for (size_t i = 0; i < warehouses.size(); ++i) {
            if (warehouses[i]->id == id) { delete warehouses[i]; warehouses.erase(warehouses.begin() + i); break; }
        }
        if (activeId == id) activeId = warehouses[0]->id;
        std::remove(invFile(id).c_str());
        audit.push("Removed warehouse " + id);
        saveAll();
        return okMsg("Warehouse removed.");
    }

    // ---- orders -------------------------------------------------------------
    bool extractFromQueue(const std::string& orderId, Order& out) {
        OrderQueue::Node* prev = NULL;
        OrderQueue::Node* cur = queue.front;
        while (cur != NULL) {
            if (cur->order.id == orderId) {
                out = cur->order;
                if (prev != NULL) prev->next = cur->next; else queue.front = cur->next;
                if (cur == queue.rear) queue.rear = prev;
                delete cur;
                return true;
            }
            prev = cur; cur = cur->next;
        }
        return false;
    }
    void updateHistory(const Order& o) {
        for (size_t i = 0; i < history.size(); ++i) if (history[i].id == o.id) { history[i] = o; return; }
    }
    void fulfill(Order& o) {
        std::string wid = orderWarehouse.count(o.id) ? orderWarehouse[o.id] : activeId;
        Warehouse* w = findWarehouse(wid);
        if (w == NULL) { o.status = "Rejected: warehouse not found"; }
        else {
            Product* p = w->inventory.find(o.productId);
            if (p == NULL) o.status = "Rejected: product not found";
            else if (p->quantity < o.quantity) o.status = "Rejected: insufficient stock";
            else {
                double unitPrice = p->price;
                p->removeStock(o.quantity);
                o.status = "Processed";
                audit.push("Processed order " + o.id);
                Sale s;
                s.orderId     = o.id;
                s.productId   = o.productId;
                s.warehouseId = w->id;
                s.quantity    = o.quantity;
                s.unitPrice   = unitPrice;
                s.amount      = unitPrice * o.quantity;
                s.date        = todayStr();
                sales.push_back(s);
            }
        }
        updateHistory(o);
    }
    Res createOrder(std::string cid, std::string pid, int qty) {
        Warehouse* w = active();
        if (w == NULL) return err("No warehouse selected.");
        if (!customerExists(cid)) return err("Customer not found. Add customer first.");
        if (w->inventory.find(pid) == NULL) return err("Product not found in " + w->id + ".");
        if (qty <= 0) return err("Quantity must be positive.");
        Order o("OR" + std::to_string(nextOrder++), cid, pid, qty);
        orderWarehouse[o.id] = w->id;
        queue.enqueue(o);
        history.push_back(o);
        audit.push("Created order " + o.id);
        saveAll();
        return okMsg("Order queued as " + o.id + ".");
    }
    Res processNext() {
        Order o;
        if (!queue.dequeue(o)) return err("No pending orders.");
        fulfill(o);
        saveAll();
        return ok(orderJson(o));
    }
    Res processOne(std::string id) {
        Order o;
        if (!extractFromQueue(id, o)) return err("Order not found in the queue.", 404);
        fulfill(o);
        saveAll();
        return ok(orderJson(o));
    }
    Res processAll() {
        std::vector<Order> results;
        Order o;
        while (queue.dequeue(o)) { fulfill(o); results.push_back(o); }
        saveAll();
        std::ostringstream out; out << "{\"count\":" << results.size() << ",\"orders\":[";
        for (size_t i = 0; i < results.size(); ++i) { if (i) out << ","; out << orderJson(results[i]); }
        out << "]}";
        return ok(out.str());
    }
    Res cancelOrder(std::string id) {
        Order o;
        if (!extractFromQueue(id, o)) return err("Order not found in the queue.", 404);
        o.status = "Cancelled";
        updateHistory(o);
        audit.push("Cancelled order " + o.id);
        saveAll();
        return okMsg("Order " + id + " cancelled.");
    }

    Res popAudit() {
        std::string action;
        if (audit.pop(action)) { saveAll(); return okMsg("Removed audit entry: " + action); }
        return err("Audit log is empty.");
    }
    Res saveNow() {
        if (saveAll()) return okMsg("Data saved.");
        return err("Save failed.", 500);
    }
};

// ----------------------------------------------------------------------------
//  HTTP plumbing
// ----------------------------------------------------------------------------
struct HttpRequest {
    std::string method, path, body;
    std::map<std::string, std::string> headers;
};

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
static std::string lower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) s[i] = (char)std::tolower((unsigned char)s[i]);
    return s;
}

static bool readRequest(int fd, HttpRequest& req) {
    std::string buf;
    char tmp[8192];
    size_t headerEnd = std::string::npos;
    while (true) {
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        buf.append(tmp, (size_t)n);
        headerEnd = buf.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
        if (buf.size() > (1u << 20)) return false;
    }
    std::string headerPart = buf.substr(0, headerEnd);
    std::string rest = buf.substr(headerEnd + 4);

    std::istringstream hs(headerPart);
    std::string line;
    std::getline(hs, line);
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::istringstream rl(line);
    std::string target;
    rl >> req.method >> target;

    while (std::getline(hs, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        size_t c = line.find(':');
        if (c != std::string::npos)
            req.headers[lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
    }
    size_t q = target.find('?');
    req.path = (q == std::string::npos) ? target : target.substr(0, q);

    size_t contentLength = 0;
    std::map<std::string, std::string>::iterator it = req.headers.find("content-length");
    if (it != req.headers.end()) contentLength = (size_t)std::strtoul(it->second.c_str(), NULL, 10);
    req.body = rest;
    while (req.body.size() < contentLength) {
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        req.body.append(tmp, (size_t)n);
    }
    if (req.body.size() > contentLength) req.body.resize(contentLength);
    return true;
}

static void sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return;
        sent += (size_t)n;
    }
}
static void sendResponse(int fd, const Res& r) {
    const char* text = "OK";
    if (r.status == 400) text = "Bad Request";
    else if (r.status == 401) text = "Unauthorized";
    else if (r.status == 403) text = "Forbidden";
    else if (r.status == 404) text = "Not Found";
    else if (r.status == 405) text = "Method Not Allowed";
    else if (r.status == 500) text = "Internal Server Error";
    std::ostringstream o;
    o << "HTTP/1.1 " << r.status << " " << text << "\r\n";
    o << "Content-Type: " << r.ctype << "\r\n";
    o << "Content-Length: " << r.body.size() << "\r\n";
    o << "Connection: close\r\n";
    o << "Access-Control-Allow-Origin: *\r\n";
    o << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    o << "Access-Control-Allow-Headers: Content-Type, X-Auth\r\n";
    o << "\r\n";
    sendAll(fd, o.str() + r.body);
}

// ----------------------------------------------------------------------------
//  Routing
// ----------------------------------------------------------------------------
static Res apiRoute(Service& s, const HttpRequest& req) {
    const std::string& m = req.method;
    const std::string& p = req.path;
    const std::string& b = req.body;

    // ---- auth -------------------------------------------------------------
    if (m == "POST" && p == "/api/login")
        return s.login(jsonGetString(b, "username"), jsonGetString(b, "password"));

    std::string token;
    std::map<std::string, std::string>::const_iterator ti = req.headers.find("x-auth");
    if (ti != req.headers.end()) token = ti->second;
    if (!s.checkToken(token)) return err("Please sign in.", 401);

    if (m == "POST" && p == "/api/logout") { s.logout(token); return okMsg("Signed out."); }

    if (m == "GET" && p == "/api/state")      return s.stateJson();
    if (m == "GET" && p == "/api/warehouses") return s.warehousesJson();
    if (m == "GET" && p == "/api/products")   return s.productsJson();
    if (m == "GET" && p == "/api/lowstock")   return s.lowStockJson();
    if (m == "GET" && p == "/api/orders")     return s.ordersJson();
    if (m == "GET" && p == "/api/customers")  return s.customersJson();
    if (m == "GET" && p == "/api/suppliers")  return s.suppliersJson();
    if (m == "GET" && p == "/api/audit")      return s.auditJson();

    if (m == "POST" && p == "/api/warehouses")
        return s.addWarehouse(jsonGetString(b, "id"), jsonGetString(b, "location"), (int)jsonGetNumber(b, "capacity", 0));
    if (m == "POST" && p == "/api/warehouses/active")
        return s.setActiveWarehouse(jsonGetString(b, "id"));
    if (m == "POST" && p == "/api/warehouses/remove")
        return s.removeWarehouse(jsonGetString(b, "id"));

    if (m == "POST" && p == "/api/products")
        return s.addProduct(jsonGetString(b, "id"), jsonGetString(b, "name"), jsonGetString(b, "category"),
                            jsonGetNumber(b, "price", 0), (int)jsonGetNumber(b, "quantity", 0),
                            (int)jsonGetNumber(b, "reorderLevel", 5));
    if (m == "POST" && p == "/api/products/restock")
        return s.restock(jsonGetString(b, "id"), (int)jsonGetNumber(b, "amount", 0));
    if (m == "POST" && p == "/api/products/price")
        return s.changePrice(jsonGetString(b, "id"), jsonGetNumber(b, "price", -1));
    if (m == "POST" && p == "/api/products/remove")
        return s.removeProduct(jsonGetString(b, "id"));

    if (m == "POST" && p == "/api/customers")
        return s.addCustomer(jsonGetString(b, "id"), jsonGetString(b, "name"), jsonGetString(b, "phone"));
    if (m == "POST" && p == "/api/suppliers")
        return s.addSupplier(jsonGetString(b, "id"), jsonGetString(b, "name"), jsonGetString(b, "phone"));

    if (m == "POST" && p == "/api/orders")
        return s.createOrder(jsonGetString(b, "customerId"), jsonGetString(b, "productId"), (int)jsonGetNumber(b, "quantity", 0));
    if (m == "POST" && p == "/api/orders/process")     return s.processNext();
    if (m == "POST" && p == "/api/orders/process-one") return s.processOne(jsonGetString(b, "id"));
    if (m == "POST" && p == "/api/orders/process-all") return s.processAll();
    if (m == "POST" && p == "/api/orders/cancel")      return s.cancelOrder(jsonGetString(b, "id"));

    if (m == "POST" && p == "/api/audit/pop") return s.popAudit();
    if (m == "POST" && p == "/api/save")      return s.saveNow();

    return err("Unknown endpoint: " + p, 404);
}

static bool readFileToString(const std::string& path, std::string& out) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
static std::string mimeFor(const std::string& path) {
    struct { const char* ext; const char* type; } table[] = {
        {".html", "text/html; charset=utf-8"}, {".css", "text/css; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"}, {".json", "application/json"},
        {".svg", "image/svg+xml"}, {".png", "image/png"}, {".ico", "image/x-icon"}, {".woff2", "font/woff2"},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        std::string ext = table[i].ext;
        if (path.size() >= ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0)
            return table[i].type;
    }
    return "application/octet-stream";
}
static Res serveStatic(const std::string& webRoot, const std::string& path) {
    std::string rel = path;
    if (rel.empty() || rel == "/") rel = "/index.html";
    if (rel.find("..") != std::string::npos) return err("Forbidden", 403);
    std::string full = webRoot + rel;
    std::string content;
    if (!readFileToString(full, content)) return err("Not found: " + rel, 404);
    return fileRes(content, mimeFor(full));
}
static Res route(Service& s, const std::string& webRoot, const HttpRequest& req) {
    if (req.method == "OPTIONS") return okMsg("ok");
    if (req.path.compare(0, 4, "/api") == 0) return apiRoute(s, req);
    if (req.method == "GET") return serveStatic(webRoot, req.path);
    return err("Method not allowed", 405);
}

// ----------------------------------------------------------------------------
//  Locate the web/ folder even when launched from elsewhere
// ----------------------------------------------------------------------------
static bool fileExists(const std::string& p) { std::ifstream f(p.c_str()); return (bool)f; }
static std::string dirOf(const std::string& path) {
    size_t p = path.find_last_of('/');
    if (p == std::string::npos) return ".";
    if (p == 0) return "/";
    return path.substr(0, p);
}
static std::string executableDir() {
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        char real[4096];
        if (realpath(buf, real) != NULL) return dirOf(std::string(real));
        return dirOf(std::string(buf));
    }
#endif
    return "";
}
static std::string resolveWebRoot(const std::string& given) {
    std::vector<std::string> candidates;
    candidates.push_back(given);
    std::string ed = executableDir();
    if (!ed.empty()) {
        candidates.push_back(ed + "/" + given);
        candidates.push_back(ed + "/../" + given);
    }
    for (size_t i = 0; i < candidates.size(); ++i)
        if (fileExists(candidates[i] + "/index.html")) return candidates[i];
    return given;
}

// ----------------------------------------------------------------------------
//  Server main
// ----------------------------------------------------------------------------
static Service* g_service = NULL;
static void onSignal(int) {
    if (g_service) g_service->saveAll();
    std::cout << "\nWIMS server stopped. Data saved.\n";
    std::exit(0);
}

int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1) port = std::atoi(argv[1]);
    std::string webRoot = "web";
    if (argc > 2) webRoot = argv[2];
    webRoot = resolveWebRoot(webRoot);

    std::srand((unsigned)std::time(NULL));

    Service service;
    g_service = &service;
    std::signal(SIGINT, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) { std::cerr << "Failed to create socket.\n"; return 1; }
    int on = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (::bind(serverFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Port " << port << " is busy. Try: ./wims_server " << (port + 1) << "\n";
        return 1;
    }
    if (listen(serverFd, 64) < 0) { std::cerr << "listen failed.\n"; return 1; }

    std::cout << "============================================\n";
    std::cout << "  WIMS Web Backend  (C++ / POSIX sockets)\n";
    std::cout << "  Open:  http://localhost:" << port << "\n";
    std::cout << "  Login: admin / admin123\n";
    std::cout << "  Web root: " << webRoot << "/\n";
    std::cout << "  Press Ctrl+C to stop (data is auto-saved)\n";
    std::cout << "============================================\n";
    std::cout.flush();

    while (true) {
        sockaddr_in client;
        socklen_t len = sizeof(client);
        int clientFd = accept(serverFd, (sockaddr*)&client, &len);
        if (clientFd < 0) continue;
        HttpRequest req;
        if (readRequest(clientFd, req)) sendResponse(clientFd, route(service, webRoot, req));
        close(clientFd);
    }
    close(serverFd);
    return 0;
}
