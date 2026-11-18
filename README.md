# WIMS — Warehouse & Inventory Management System

WIMS is a warehouse and inventory management system written in C++17. The
domain logic lives in `pbl.cpp`: products are stored in a singly linked list,
orders are handled through a FIFO queue, and every action is recorded in a LIFO
audit stack. A lightweight HTTP server (`server.cpp`, POSIX sockets) exposes
that logic as a JSON API and serves the browser dashboard in `web/`. All state
is persisted to tab-separated text files.

`pbl.cpp` is unchanged. `server.cpp` includes it directly and reuses its
classes, so the terminal program and the web dashboard share one implementation.

---

## Run it

```bash
./run.sh            # builds and starts on http://localhost:8080
./run.sh 8081       # or choose another port
```

Then open **http://localhost:8080**.

**Admin login:** `admin` / `admin123`

Stop with `Ctrl+C` (data is saved automatically). The old console version still
works: `make console && ./wims_console`.

---

## Features

- **Admin login** (checked by the C++ backend) with a Logout button.
- **Multiple warehouses** — add / remove warehouses and switch the active one
  from the top bar. Each warehouse has its **own products, stock and capacity**.
- **Inventory** — add, search, restock, change price, delete (per warehouse).
- **Orders** — create, process the next / a specific order, **process all**, and
  **cancel** orders. Each order remembers which warehouse it belongs to and warns
  when there isn't enough stock.
- **Customers & suppliers** management.
- **Audit log** of every action, with "remove latest".
- **Dark / light mode** toggle (remembered between visits).
- Low-stock alerts, dashboard totals, and automatic saving to text files.

---

## Folder layout

| File | Purpose |
|------|---------|
| `pbl.cpp` | The original console program + **all classes** (unchanged). |
| `server.cpp` | C++ HTTP backend: reuses `pbl.cpp`'s classes, exposes a JSON API, serves the dashboard. |
| `web/index.html` | Dashboard layout + login screen. |
| `web/styles.css` | Bold, minimalist theme (dark + light). |
| `web/app.js` | Front-end logic (fetch → `/api/*`). |
| `Makefile` / `run.sh` | Build & run helpers. |

Data files (created automatically): `warehouses.txt`, `inventory_<id>.txt`,
`orders.txt`, `order_warehouse.txt`, `customers.txt`, `suppliers.txt`,
`audit_log.txt`.

---

## Data structures (DSA)

| Structure | Where |
|-----------|-------|
| **Singly linked list** | `InventoryList` — every product is a node |
| **Queue (FIFO)** | `OrderQueue` — orders processed first-in-first-out |
| **Stack (LIFO)** | `AuditStack` — action history, push/pop |

---

## API reference

| Method | Endpoint | Action |
|--------|----------|--------|
| POST | `/api/login` | Sign in (returns a token) |
| POST | `/api/logout` | Sign out |
| GET  | `/api/state` | Dashboard totals + warehouses |
| GET  | `/api/warehouses` | List warehouses |
| POST | `/api/warehouses` · `/api/warehouses/active` · `/api/warehouses/remove` | Manage warehouses |
| GET  | `/api/products` · `/api/lowstock` | Inventory (active warehouse) |
| POST | `/api/products` · `/api/products/restock` · `/api/products/price` · `/api/products/remove` | Manage products |
| GET/POST | `/api/customers` · `/api/suppliers` | People |
| GET  | `/api/orders` | Pending queue + history |
| POST | `/api/orders` · `/api/orders/process` · `/api/orders/process-one` · `/api/orders/process-all` · `/api/orders/cancel` | Orders |
| GET/POST | `/api/audit` · `/api/audit/pop` | Audit log |
| POST | `/api/save` | Persist to files |

All endpoints except `/api/login` require the `X-Auth` header with the token
returned by login.

No third-party libraries are used — the HTTP server is plain C++ with POSIX
sockets, so it builds with just `clang++`/`g++`.
