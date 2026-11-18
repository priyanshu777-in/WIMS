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

---

## Module notes

### Employee base class & linked-list support — Priyanshu Negi

- `Employee` holds `id`, `name` and `role`, with a default role of `Staff`.
- `describe()` and `canManage()` are declared `virtual` so subclasses can change
  behaviour at runtime; the destructor is virtual as well.
- `Manager` inherits from `Employee`, calls the base constructor with the role
  `Manager`, and overrides both methods (`canManage()` returns `true`).
- The `InventoryList` node structure and its pointer operations (`add`, `find`,
  `remove`, `clear`, save/load) are implemented together with the team lead.
- `server.cpp` uses the same `Manager` object: login returns its `name` and
  `role`, and product deletion is guarded by `canManage()`.
