# Contributors

WIMS is built by a six-member team. Each member owns one module inside `pbl.cpp`.

| Member | GitHub | Module | Location |
|--------|--------|--------|----------|
| Priyanshu | @priyanshu777-in | `Manager`, `InventoryList` (singly linked list) | `pbl.cpp` 56–177 |
| Priyanshu Negi | @PriyanshuNegiii | `Employee` base class, linked-list support | `pbl.cpp` 163–177 |
| Saurabh Singh Negi | — | `Order`, `AuditStack` (LIFO) | `pbl.cpp` 205–282 |
| Jeevan | — | `Supplier`, `Customer`, stack support | `pbl.cpp` 190–203 |
| Harsh Yadav | — | `Customer`, `OrderQueue` (FIFO) | `pbl.cpp` 228–251 |
| Ankit Singh Negi | @asnegi808-beep | `Warehouse`, queue support | `pbl.cpp` 179–188 |

The web layer (`server.cpp`, `web/`) reuses these classes without changing `pbl.cpp`.
