#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
using namespace std;

// -------------------- PRODUCT --------------------
class Product {
public:
    string id, name, category;
    double price;
    int quantity, reorderLevel;

    Product(string i = "", string n = "", string c = "", double p = 0,
            int q = 0, int r = 5) {
        id = i; name = n; category = c;
        price = p; quantity = q; reorderLevel = r;
    }

    void addStock(int amount) { quantity += amount; }

    bool removeStock(int amount) {
        if (amount <= 0 || amount > quantity) return false;
        quantity -= amount;
        return true;
    }

    void display() const {
        cout << left << setw(12) << id << setw(20) << name
             << setw(16) << category << right << setw(10)
             << fixed << setprecision(2) << price << setw(9) << quantity
             << setw(10) << reorderLevel;
        if (quantity <= reorderLevel) cout << "  LOW STOCK";
        cout << '\n';
    }

    string toFile() const {
        ostringstream out;
        out << id << '\t' << name << '\t' << category << '\t'
            << price << '\t' << quantity << '\t' << reorderLevel;
        return out.str();
    }

    static Product fromFile(string line) {
        stringstream in(line);
        string i, n, c;
        double p; int q, r;
        getline(in, i, '\t'); getline(in, n, '\t'); getline(in, c, '\t');
        in >> p; in.ignore(1); in >> q; in.ignore(1); in >> r;
        return Product(i, n, c, p, q, r);
    }
};

// -------------------- INVENTORY: SINGLY LINKED LIST --------------------
class InventoryList {
    struct Node {
        Product product;
        Node* next;
        Node(Product p) : product(p), next(NULL) {}
    };
    Node* head;

public:
    InventoryList() { head = NULL; }
    ~InventoryList() { clear(); }

    void clear() {
        while (head != NULL) {
            Node* temp = head;
            head = head->next;
            delete temp;
        }
    }

    Product* find(string id) {
        Node* temp = head;
        while (temp != NULL) {
            if (temp->product.id == id) return &temp->product;
            temp = temp->next;
        }
        return NULL;
    }

    bool add(Product p) {
        if (find(p.id) != NULL) return false;
        Node* node = new Node(p);
        if (head == NULL) head = node;
        else {
            Node* temp = head;
            while (temp->next != NULL) temp = temp->next;
            temp->next = node;
        }
        return true;
    }

    bool remove(string id) {
        Node* current = head;
        Node* previous = NULL;
        while (current != NULL && current->product.id != id) {
            previous = current;
            current = current->next;
        }
        if (current == NULL) return false;
        if (previous == NULL) head = current->next;
        else previous->next = current->next;
        delete current;
        return true;
    }

    void display() const {
        cout << left << setw(12) << "ID" << setw(20) << "Name"
             << setw(16) << "Category" << right << setw(10) << "Price"
             << setw(9) << "Stock" << setw(10) << "Reorder" << '\n';
        cout << string(77, '-') << '\n';
        if (head == NULL) { cout << "Inventory is empty.\n"; return; }
        Node* temp = head;
        while (temp != NULL) { temp->product.display(); temp = temp->next; }
    }

    void showLowStock() const {
        bool found = false;
        Node* temp = head;
        while (temp != NULL) {
            if (temp->product.quantity <= temp->product.reorderLevel) {
                temp->product.display(); found = true;
            }
            temp = temp->next;
        }
        if (!found) cout << "No low-stock products.\n";
    }

    int totalQuantity() const {
        int total = 0;
        Node* temp = head;
        while (temp != NULL) { total += temp->product.quantity; temp = temp->next; }
        return total;
    }

    bool save() const {
        ofstream file("inventory.txt");
        if (!file) return false;
        Node* temp = head;
        while (temp != NULL) { file << temp->product.toFile() << '\n'; temp = temp->next; }
        return file.good();
    }

    void load() {
        ifstream file("inventory.txt");
        if (!file) return;
        clear();
        string line;
        while (getline(file, line)) {
            if (line.empty()) continue;
            try { add(Product::fromFile(line)); }
            catch (...) { cout << "Skipping invalid product record.\n"; }
        }
    }
};

// -------------------- EMPLOYEE AND MANAGER --------------------
class Employee {
public:
    string id, name, role;
    Employee(string i = "", string n = "", string r = "Staff") : id(i), name(n), role(r) {}
    virtual string describe() { return name + " (" + role + ")"; }
    virtual bool canManage() { return false; }
    virtual ~Employee() {}
};

class Manager : public Employee {
public:
    Manager(string i = "E001", string n = "Project Manager") : Employee(i, n, "Manager") {}
    string describe() override { return name + " (Manager: full access)"; }
    bool canManage() override { return true; }
};

// -------------------- WAREHOUSE --------------------
class Warehouse {
public:
    string id, location;
    int capacity;
    InventoryList inventory;
    Warehouse() { id = "WH-01";
         location = "Main Warehouse"; 
         capacity = 1000; }
};

// -------------------- CUSTOMER AND SUPPLIER --------------------
class Customer {
public:
    string id, name, phone;
    Customer(string i = "", string n = "", string p = "") : id(i), name(n), phone(p) {}
    string toFile() const { return id + "\t" + name + "\t" + phone; }
};

class Supplier {
public:
    string id, name, phone;
    Supplier(string i = "", string n = "", string p = "") : id(i), name(n), phone(p) {}
    string toFile() const { return id + "\t" + name + "\t" + phone; }
};

// -------------------- ORDER AND FIFO QUEUE --------------------
class Order {
public:
    string id, customerId, productId, status;
    int quantity;
    Order(string oi = "", string ci = "", string pi = "", int q = 0) {
        id = oi; customerId = ci; productId = pi; quantity = q; status = "Pending";
    }
    void display() const {
        cout << id << " | customer=" << customerId << " | product=" << productId
             << " | qty=" << quantity << " | " << status << '\n';
    }
    string toFile() const {
        return id + "\t" + customerId + "\t" + productId + "\t" + to_string(quantity) + "\t" + status;
    }
    static Order fromFile(string line) {
        stringstream in(line); string oi, ci, pi, q, s;
        getline(in, oi, '\t'); getline(in, ci, '\t'); getline(in, pi, '\t');
        getline(in, q, '\t'); getline(in, s, '\t');
        Order o(oi, ci, pi, stoi(q)); o.status = s; return o;
    }
};

class OrderQueue {
    struct Node { Order order; Node* next; Node(Order o) : order(o), next(NULL) {} };
    Node *front, *rear;
public:
    OrderQueue() { front = rear = NULL; }
    ~OrderQueue() { while (front != NULL) { Node* t = front; front = front->next; delete t; } }
    bool empty() const { return front == NULL; }
    void enqueue(Order o) {
        Node* node = new Node(o);
        if (rear == NULL) front = rear = node;
        else { rear->next = node; rear = node; }
    }
    bool dequeue(Order& result) {
        if (empty()) return false;
        Node* temp = front; result = temp->order; front = front->next;
        if (front == NULL) rear = NULL;
        delete temp; return true;
    }
    void display() const {
        if (empty()) { cout << "No pending orders.\n"; return; }
        Node* temp = front;
        while (temp != NULL) { temp->order.display(); temp = temp->next; }
    }
};

// -------------------- AUDIT STACK: LIFO --------------------
class AuditStack {
    struct Node { string action; Node* next; Node(string a, Node* n) : action(a), next(n) {} };
    Node* top;
public:
    AuditStack() { top = NULL; }
    ~AuditStack() { while (top != NULL) { Node* t = top; top = top->next; delete t; } }
    void push(string action) { top = new Node(action, top); }
    bool pop(string& action) {
        if (top == NULL) return false;
        Node* temp = top; action = temp->action; top = top->next; delete temp; return true;
    }
    void display() const {
        if (top == NULL) { cout << "No audit activity yet.\n"; return; }
        Node* temp = top;
        while (temp != NULL) { cout << "- " << temp->action << '\n'; temp = temp->next; }
    }
    bool save() const {
        ofstream file("audit_log.txt"); if (!file) return false;
        Node* temp = top;
        while (temp != NULL) { file << temp->action << '\n'; temp = temp->next; }
        return file.good();
    }
    void load() {
        ifstream file("audit_log.txt"); if (!file) return;
        vector<string> actions; string line;
        while (getline(file, line)) if (!line.empty()) actions.push_back(line);
        for (int i = (int)actions.size() - 1; i >= 0; i--) push(actions[i]);
    }
};

// -------------------- MAIN APPLICATION --------------------
class WIMS {
    Warehouse warehouse;
    Manager manager;
    OrderQueue queue;
    AuditStack audit;
    vector<Customer> customers;
    vector<Supplier> suppliers;
    vector<Order> history;
    int nextOrder = 1;

    string readText(string prompt) { string value; cout << prompt; getline(cin, value); return value; }
    int readInt(string prompt) {
        int value; string line;
        while (true) {
            cout << prompt; getline(cin, line); stringstream input(line);
            char extra;
            if (input >> value && !(input >> extra)) return value;
            cout << "Enter a valid whole number.\n";
        }
    }
    double readDouble(string prompt) {
        double value; string line;
        while (true) {
            cout << prompt; getline(cin, line); stringstream input(line); char extra;
            if (input >> value && !(input >> extra)) return value;
            cout << "Enter a valid number.\n";
        }
    }
    bool customerExists(string id) const {
        for (const Customer& c : customers) if (c.id == id) return true;
        return false;
    }
    void saveCustomers() const {
        ofstream file("customers.txt"); for (const Customer& c : customers) file << c.toFile() << '\n';
    }
    void saveSuppliers() const {
        ofstream file("suppliers.txt"); for (const Supplier& s : suppliers) file << s.toFile() << '\n';
    }
    void loadPeople() {
        string line;
        ifstream cf("customers.txt");
        while (getline(cf, line)) {
            stringstream in(line); string i, n, p;
            if (getline(in, i, '\t') && getline(in, n, '\t') && getline(in, p, '\t')) customers.emplace_back(i, n, p);
        }
        ifstream sf("suppliers.txt");
        while (getline(sf, line)) {
            stringstream in(line); string i, n, p;
            if (getline(in, i, '\t') && getline(in, n, '\t') && getline(in, p, '\t')) suppliers.emplace_back(i, n, p);
        }
    }
    void loadOrders() {
        ifstream file("orders.txt"); string line;
        while (getline(file, line)) {
            if (line.empty()) continue;
            try {
                Order o = Order::fromFile(line); history.push_back(o);
                if (o.status == "Pending") queue.enqueue(o);
                if (o.id.size() > 2) nextOrder = max(nextOrder, stoi(o.id.substr(2)) + 1);
            } catch (...) { cout << "Skipping invalid order record.\n"; }
        }
    }
    bool saveAll() {
        bool ok = warehouse.inventory.save();
        ofstream orders("orders.txt");
        if (!orders) ok = false;
        else { for (const Order& o : history) orders << o.toFile() << '\n'; if (!orders.good()) ok = false; }
        if (!audit.save()) ok = false;
        saveCustomers(); saveSuppliers();
        return ok;
    }
    void addProduct() {
        string id = readText("Product ID: ");
        string name = readText("Name: ");
        string category = readText("Category: ");
        double price = readDouble("Price: ");
        int quantity = readInt("Initial stock: ");
        int reorder = readInt("Reorder threshold: ");
        if (id.empty() || name.empty() || category.empty() || price < 0 || quantity < 0 || reorder < 0) {
            cout << "Invalid product details.\n"; return;
        }
        if (warehouse.inventory.totalQuantity() + quantity > warehouse.capacity) {
            cout << "Warehouse capacity exceeded.\n"; return;
        }
        if (!warehouse.inventory.add(Product(id, name, category, price, quantity, reorder))) {
            cout << "Product ID already exists.\n"; return;
        }
        audit.push("Added product " + id); cout << "Product added.\n";
    }
    void searchProduct() {
        string id = readText("Product ID: "); Product* p = warehouse.inventory.find(id);
        if (p == NULL) cout << "Product not found.\n"; else p->display();
    }
    void restockProduct() {
        string id = readText("Product ID: "); Product* p = warehouse.inventory.find(id);
        if (p == NULL) { cout << "Product not found.\n"; return; }
        int amount = readInt("Quantity to add: ");
        if (amount <= 0) { cout << "Quantity must be positive.\n"; return; }
        if (warehouse.inventory.totalQuantity() + amount > warehouse.capacity) { cout << "Warehouse capacity exceeded.\n"; return; }
        p->addStock(amount); audit.push("Restocked " + id); cout << "Stock updated.\n";
    }
    void addCustomer() {
        string id = readText("Customer ID: "), name = readText("Customer name: "), phone = readText("Phone: ");
        if (id.empty() || name.empty() || phone.empty() || customerExists(id)) { cout << "Invalid details or customer ID already exists.\n"; return; }
        customers.emplace_back(id, name, phone); audit.push("Added customer " + id); cout << "Customer added.\n";
    }
    void addSupplier() {
        string id = readText("Supplier ID: "), name = readText("Supplier name: "), phone = readText("Phone: ");
        for (const Supplier& s : suppliers) if (s.id == id) { cout << "Supplier ID already exists.\n"; return; }
        if (id.empty() || name.empty() || phone.empty()) { cout << "All fields are required.\n"; return; }
        suppliers.emplace_back(id, name, phone); audit.push("Added supplier " + id); cout << "Supplier added.\n";
    }
    void createOrder() {
        string cid = readText("Customer ID: "), pid = readText("Product ID: ");
        if (!customerExists(cid)) { cout << "Customer not found. Add customer first.\n"; return; }
        if (warehouse.inventory.find(pid) == NULL) { cout << "Product not found.\n"; return; }
        int quantity = readInt("Quantity: ");
        if (quantity <= 0) { cout << "Quantity must be positive.\n"; return; }
        Order o("OR" + to_string(nextOrder++), cid, pid, quantity);
        queue.enqueue(o); history.push_back(o); audit.push("Created order " + o.id);
        cout << "Order queued as " << o.id << ".\n";
    }
    void processOrder() {
        Order o;
        if (!queue.dequeue(o)) { cout << "Order queue is empty.\n"; return; }
        Product* p = warehouse.inventory.find(o.productId);
        if (p == NULL) o.status = "Rejected: product not found";
        else if (p->quantity < o.quantity) o.status = "Rejected: insufficient stock";
        else { p->removeStock(o.quantity); o.status = "Processed"; audit.push("Processed order " + o.id); }
        for (Order& old : history) if (old.id == o.id) { old = o; break; }
        o.display();
    }
    void showOrders() const {
        cout << "--- Pending queue (FIFO) ---\n"; queue.display();
        cout << "--- Order history ---\n";
        if (history.empty()) cout << "No order history.\n";
        for (const Order& o : history) o.display();
    }
    void removeProduct() {
        if (!manager.canManage()) { cout << "Manager access required.\n"; return; }
        string id = readText("Product ID to remove: ");
        if (warehouse.inventory.remove(id)) { audit.push("Removed product " + id); cout << "Product removed.\n"; }
        else cout << "Product not found.\n";
    }
    void changePrice() {
        string id = readText("Product ID: "); Product* p = warehouse.inventory.find(id);
        if (p == NULL) { cout << "Product not found.\n"; return; }
        double price = readDouble("New price: ");
        if (price < 0) { cout << "Price cannot be negative.\n"; return; }
        p->price = price; audit.push("Changed price of " + id); cout << "Price updated.\n";
    }

public:
    WIMS() {
        warehouse.inventory.load(); loadPeople(); loadOrders(); audit.load();
    }
    void run() {
        cout << "====================================\n";
        cout << " WIMS - Warehouse & Inventory System\n";
        cout << "Warehouse: " << warehouse.id << " | " << warehouse.location << '\n';
        cout << "Signed in as: " << manager.describe() << '\n';
        cout << "====================================\n";
        int choice;
        do {
            cout << "\n1. Add product\n2. View inventory\n3. Search product\n4. Restock product\n"
                 << "5. Show low-stock alerts\n6. Create customer order\n7. Process next order\n"
                 << "8. View order queue/history\n9. Add customer\n10. Add supplier\n"
                 << "11. View audit log\n12. Remove latest audit entry\n13. Save now\n"
                 << "14. Remove product\n15. Change product price\n0. Exit\n";
            choice = readInt("Select: ");
            switch (choice) {
                case 1: addProduct(); break;
                case 2: warehouse.inventory.display(); break;
                case 3: searchProduct(); break;
                case 4: restockProduct(); break;
                case 5: warehouse.inventory.showLowStock(); break;
                case 6: createOrder(); break;
                case 7: processOrder(); break;
                case 8: showOrders(); break;
                case 9: addCustomer(); break;
                case 10: addSupplier(); break;
                case 11: audit.display(); break;
                case 12: { string action; if (audit.pop(action)) cout << "Removed audit entry: " << action << '\n'; else cout << "Audit log is empty.\n"; break; }
                case 13: if (saveAll()) cout << "Data saved.\n"; else cout << "Save failed.\n"; break;
                case 14: removeProduct(); break;
                case 15: changePrice(); break;
                case 0: if (saveAll()) cout << "Data saved. Goodbye.\n"; else cout << "Save failed. Goodbye.\n"; break;
                default: cout << "Invalid option.\n";
            }
        } while (choice != 0);
    }
};

int main() {
    WIMS app;
    app.run();
    return 0;
}