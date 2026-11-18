/* ============================================================================
   WIMS Dashboard — front-end logic (vanilla JS, no build step)
   ========================================================================== */

const $  = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

function esc(s) {
  return String(s == null ? "" : s).replace(/[&<>"']/g, c =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}
const money = n =>
  "₹" + Number(n || 0).toLocaleString("en-IN", { minimumFractionDigits: 2, maximumFractionDigits: 2 });

/* ------------------------------ session --------------------------------- */
let AUTH_TOKEN = localStorage.getItem("wims_token") || "";

/* ------------------------------ api ------------------------------------- */
async function api(path, method = "GET", body) {
  const opts = { method, headers: {} };
  if (AUTH_TOKEN) opts.headers["X-Auth"] = AUTH_TOKEN;
  if (body !== undefined) {
    opts.headers["Content-Type"] = "application/json";
    opts.body = JSON.stringify(body);
  }
  let res;
  try {
    res = await fetch("/api" + path, opts);
  } catch (e) {
    setStatus("offline");
    throw new Error("Cannot reach the server. Is wims_server running?");
  }
  setStatus("online");
  let json;
  try { json = await res.json(); } catch (e) { json = { ok: false, error: "Bad response" }; }
  if (!json.ok) {
    if (res.status === 401 && path !== "/login") {
      signOutLocal();
      throw new Error(json.error || "Please sign in again.");
    }
    throw new Error(json.error || "Request failed");
  }
  return json;
}

function setStatus(state) {
  const el = $("#status");
  if (!el) return;
  el.classList.remove("online", "offline");
  if (state === "online")  { el.classList.add("online");  $("#status-text").textContent = "Connected"; }
  if (state === "offline") { el.classList.add("offline"); $("#status-text").textContent = "Offline"; }
}

/* ------------------------------ theme ----------------------------------- */
function applyTheme() {
  const t = localStorage.getItem("wims_theme") || "dark";
  document.documentElement.setAttribute("data-theme", t);
}
function toggleTheme() {
  const cur = document.documentElement.getAttribute("data-theme") === "light" ? "light" : "dark";
  const next = cur === "light" ? "dark" : "light";
  document.documentElement.setAttribute("data-theme", next);
  localStorage.setItem("wims_theme", next);
}

/* ------------------------------ toast ----------------------------------- */
function toast(message, type = "ok") {
  const root = $("#toast-root");
  const el = document.createElement("div");
  el.className = "toast " + type;
  el.textContent = message;
  root.appendChild(el);
  setTimeout(() => {
    el.style.transition = "opacity .25s, transform .25s";
    el.style.opacity = "0";
    el.style.transform = "translateX(16px)";
    setTimeout(() => el.remove(), 250);
  }, 3200);
}

/* ------------------------------ modals ---------------------------------- */
function closeModal() { $("#modal-root").innerHTML = ""; }

function fieldHtml(f) {
  const id = "f_" + f.name;
  if (f.type === "select") {
    const opts = (f.options || []).map(o =>
      `<option value="${esc(o.value)}">${esc(o.label)}</option>`).join("");
    return `<label class="field"><span>${esc(f.label)}</span>
      <select id="${id}" ${f.required ? "required" : ""}>${opts}</select></label>`;
  }
  const attrs = [
    f.type === "number" ? 'type="number"' : 'type="text"',
    f.step !== undefined ? `step="${f.step}"` : "",
    f.min !== undefined ? `min="${f.min}"` : "",
    f.value !== undefined ? `value="${esc(f.value)}"` : "",
    f.placeholder ? `placeholder="${esc(f.placeholder)}"` : "",
    f.required ? "required" : "",
  ].join(" ");
  return `<label class="field"><span>${esc(f.label)}</span><input id="${id}" ${attrs}></label>`;
}

function openFormModal({ title, fields, submitLabel = "Save", onSubmit }) {
  $("#modal-root").innerHTML = `
    <div class="overlay">
      <form class="modal" id="modal-form" novalidate>
        <div class="modal-head">
          <h3>${esc(title)}</h3>
          <button type="button" class="icon-btn" data-close>✕</button>
        </div>
        <div class="modal-body"><div class="form-grid">${fields.map(fieldHtml).join("")}</div></div>
        <div class="modal-foot">
          <button type="button" class="btn btn-ghost" data-close>Cancel</button>
          <button type="submit" class="btn btn-primary">${esc(submitLabel)}</button>
        </div>
      </form>
    </div>`;

  const form = $("#modal-form");
  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const values = {};
    for (const f of fields) {
      const el = $("#f_" + f.name);
      values[f.name] = f.type === "number" ? Number(el.value) : el.value.trim();
    }
    const btn = form.querySelector('button[type="submit"]');
    btn.disabled = true;
    try { await onSubmit(values); closeModal(); }
    catch (err) { toast(err.message || "Something went wrong", "error"); btn.disabled = false; }
  });
}

function openConfirm({ title, message, confirmLabel = "Confirm", onConfirm }) {
  $("#modal-root").innerHTML = `
    <div class="overlay">
      <div class="modal" style="width:min(430px,100%)">
        <div class="modal-head"><h3>${esc(title)}</h3>
          <button type="button" class="icon-btn" data-close>✕</button></div>
        <div class="modal-body"><p class="muted" style="font-size:14px">${esc(message)}</p></div>
        <div class="modal-foot">
          <button type="button" class="btn btn-ghost" data-close>Cancel</button>
          <button type="button" class="btn btn-danger" id="confirm-ok">${esc(confirmLabel)}</button>
        </div>
      </div>
    </div>`;
  $("#confirm-ok").addEventListener("click", async () => {
    try { await onConfirm(); closeModal(); }
    catch (err) { toast(err.message || "Failed", "error"); }
  });
}

/* ------------------------------ views ----------------------------------- */
const VIEWS = {
  dashboard:  { title: "Dashboard",  sub: "Overview of warehouse activity",         render: viewDashboard },
  inventory:  { title: "Inventory",  sub: "Products stored in the active warehouse", render: viewInventory },
  orders:     { title: "Orders",     sub: "Create, process and track orders",        render: viewOrders },
  warehouses: { title: "Warehouses", sub: "Manage your storage locations",           render: viewWarehouses },
  customers:  { title: "Customers",  sub: "People who place orders",                 render: viewCustomers },
  suppliers:  { title: "Suppliers",  sub: "Vendors who supply stock",                render: viewSuppliers },
  audit:      { title: "Audit Log",  sub: "Most recent actions first",               render: viewAudit },
};
let currentView = "dashboard";

async function navigate(view) {
  currentView = view;
  $$(".nav-item").forEach(b => b.classList.toggle("active", b.dataset.view === view));
  const meta = VIEWS[view];
  $("#view-title").textContent = meta.title;
  $("#view-sub").textContent = meta.sub;
  $("#view").innerHTML = `<div class="empty">Loading…</div>`;
  try {
    await loadWarehousePicker();
    await meta.render();
  } catch (err) {
    $("#view").innerHTML = `<div class="empty">${esc(err.message)}</div>`;
  }
}
async function refresh() { return navigate(currentView); }

/* ------------------------------ warehouse picker ------------------------ */
async function loadWarehousePicker() {
  const { data: list } = await api("/warehouses");
  const sel = $("#wh-select");
  if (!sel) return;
  sel.innerHTML = list.map(w =>
    `<option value="${esc(w.id)}"${w.active ? " selected" : ""}>${esc(w.id)} · ${esc(w.location)}</option>`
  ).join("");
}

/* ------------------------------ shared fragments ------------------------ */
function productRows(products) {
  if (!products.length)
    return `<tr><td colspan="8"><div class="empty">No products here yet. Click “Add product” to begin.</div></td></tr>`;
  return products.map(p => `
    <tr data-row data-search="${esc((p.id + " " + p.name + " " + p.category).toLowerCase())}">
      <td class="mono strong">${esc(p.id)}</td>
      <td>${esc(p.name)}</td>
      <td class="dim">${esc(p.category)}</td>
      <td class="num mono">${money(p.price)}</td>
      <td class="num mono strong">${p.quantity}</td>
      <td class="num mono dim">${p.reorderLevel}</td>
      <td>${p.lowStock ? '<span class="badge low">Low stock</span>'
                       : '<span class="badge ok">In stock</span>'}</td>
      <td class="num">
        <div style="display:flex;gap:6px;justify-content:flex-end">
          <button class="btn btn-sm btn-ghost" data-action="restock" data-id="${esc(p.id)}">Restock</button>
          <button class="btn btn-sm btn-ghost" data-action="price" data-id="${esc(p.id)}">Price</button>
          <button class="btn btn-sm btn-danger" data-action="delete-product"
                  data-id="${esc(p.id)}" data-name="${esc(p.name)}">Delete</button>
        </div>
      </td>
    </tr>`).join("");
}
function productTable(products) {
  return `<div class="table-wrap"><table>
    <thead><tr>
      <th>ID</th><th>Name</th><th>Category</th>
      <th class="num">Price</th><th class="num">Stock</th><th class="num">Reorder</th>
      <th>Status</th><th class="num">Actions</th>
    </tr></thead>
    <tbody>${productRows(products)}</tbody>
  </table></div>`;
}
function personRows(list) {
  if (!list.length) return `<tr><td colspan="3"><div class="empty">Nothing here yet.</div></td></tr>`;
  return list.map(p => `
    <tr data-row data-search="${esc((p.id + " " + p.name + " " + p.phone).toLowerCase())}">
      <td class="mono strong">${esc(p.id)}</td>
      <td>${esc(p.name)}</td>
      <td class="dim mono">${esc(p.phone)}</td>
    </tr>`).join("");
}

/* ------------------------------ dashboard ------------------------------- */
async function viewDashboard() {
  const [s, low, audit] = await Promise.all([api("/state"), api("/lowstock"), api("/audit")]);
  const t = s.data.totals;
  const kpi = (label, value, foot, cls = "") => `
    <div class="card kpi ${cls}">
      <div class="kpi-label">${label}</div>
      <div class="kpi-value">${value}</div>
      <div class="kpi-foot">${foot}</div>
    </div>`;

  const lowTable = low.data.length
    ? `<div class="table-wrap"><table>
        <thead><tr><th>ID</th><th>Name</th><th class="num">Stock</th><th class="num">Reorder</th></tr></thead>
        <tbody>${low.data.map(p => `
          <tr><td class="mono strong">${esc(p.id)}</td><td>${esc(p.name)}</td>
          <td class="num mono" style="color:var(--warn)">${p.quantity}</td>
          <td class="num mono dim">${p.reorderLevel}</td></tr>`).join("")}
        </tbody></table></div>`
    : `<div class="empty">All products are above their reorder level. 🎉</div>`;

  const auditHtml = audit.data.slice(0, 6).length
    ? audit.data.slice(0, 6).map((a, i) => `
        <div class="audit-row"><span class="audit-dot"></span><span>${esc(a)}</span>
          <span class="when">${i === 0 ? "latest" : "#" + (i + 1)}</span></div>`).join("")
    : `<div class="empty">No activity recorded yet.</div>`;

  const whRows = s.data.warehouses.map(w => `
    <div class="order-card">
      <div class="oc-main">
        <div class="oc-title">${esc(w.id)} · ${esc(w.location)} ${w.active ? '<span class="badge ok">Active</span>' : ""}</div>
        <div class="oc-sub">${w.stock} / ${w.capacity} units · ${w.products} products</div>
      </div>
      ${w.active ? "" : `<button class="btn btn-sm btn-ghost" data-action="set-warehouse" data-id="${esc(w.id)}">Switch</button>`}
    </div>`).join("");

  $("#view").innerHTML = `
    <div class="grid kpis">
      ${kpi("Total sales", money(t.totalSales), `${t.salesCount} sale${t.salesCount === 1 ? "" : "s"} · lifetime`, "money")}
      ${kpi("Sales today", money(t.salesToday), `${t.salesTodayCount} order${t.salesTodayCount === 1 ? "" : "s"} today`, "money")}
      ${kpi("Stock value", money(t.stockValue), `active warehouse · ${money(t.stockValueAll)} in all`, "money")}
    </div>

    <div class="grid kpis mt">
      ${kpi("Products", t.products, "in active warehouse")}
      ${kpi("Total stock", t.stock, "units on hand")}
      ${kpi("Low stock", t.lowStock, "need restocking", t.lowStock > 0 ? "warn" : "")}
      ${kpi("Pending orders", t.pending, "waiting to be processed")}
      ${kpi("Customers", t.customers, "registered")}
      ${kpi("Warehouses", t.warehouses, "locations")}
    </div>

    <div class="grid mt" style="grid-template-columns:1.3fr 1fr;gap:18px">
      <div class="card">
        <div class="card-head"><h2>Low-stock alerts</h2><span class="hint">Active warehouse</span></div>
        ${lowTable}
      </div>
      <div class="card">
        <div class="card-head"><h2>Recent activity</h2><span class="hint">Most recent first</span></div>
        <div class="audit-list">${auditHtml}</div>
      </div>
    </div>

    <div class="card mt">
      <div class="card-head"><h2>Warehouses</h2><span class="hint">Active: ${esc(s.data.warehouse.id)}</span></div>
      <div class="stack" style="gap:10px">${whRows}</div>
    </div>`;
}

/* ------------------------------ inventory ------------------------------- */
async function viewInventory() {
  const { data: products } = await api("/products");
  $("#view").innerHTML = `
    <div class="toolbar">
      <div class="search grow">
        <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/></svg>
        <input placeholder="Search by ID, name or category…" data-search-input>
      </div>
      <button class="btn btn-primary" data-action="add-product">+ Add product</button>
    </div>
    <div class="card">${productTable(products)}</div>`;
}

/* ------------------------------ orders ---------------------------------- */
async function viewOrders() {
  const [orders, customers, products] = await Promise.all([
    api("/orders"), api("/customers"), api("/products"),
  ]);

  const custOpts = customers.data.map(c =>
    `<option value="${esc(c.id)}">${esc(c.id)} — ${esc(c.name)}</option>`).join("");
  const prodOpts = products.data.map(p =>
    `<option value="${esc(p.id)}">${esc(p.id)} — ${esc(p.name)}</option>`).join("");
  const canOrder = customers.data.length && products.data.length;
  const priceOf = {};
  products.data.forEach(p => { priceOf[p.id] = Number(p.price) || 0; });

  const createCard = canOrder
    ? `<form data-form="create-order" class="form-grid">
         <label class="field"><span>Customer</span><select name="customerId" required>${custOpts}</select></label>
         <label class="field"><span>Product</span><select name="productId" required>${prodOpts}</select></label>
         <label class="field"><span>Quantity</span><input type="number" name="quantity" min="1" value="1" required></label>
         <div class="field"><span>Total cost</span><div class="order-total" id="order-total">₹0.00</div></div>
         <div class="field"><span>&nbsp;</span><button class="btn btn-primary" type="submit">Create order</button></div>
       </form>`
    : `<div class="empty">Add at least one customer and one product (in this warehouse) before creating orders.</div>`;

  const pending = orders.data.pending;
  const queueHtml = pending.length
    ? `<div class="stack" style="gap:10px">${pending.map((o, i) => `
        <div class="order-card">
          <span class="pos">${i + 1}</span>
          <div class="oc-main">
            <div class="oc-title">${esc(o.id)} · ${o.quantity} × ${esc(o.productId)} · <span style="color:var(--ok)">${money(o.amount)}</span></div>
            <div class="oc-sub">customer ${esc(o.customerId)} · ${esc(o.warehouseName)} · ${money(o.unitPrice)} each · in stock ${o.available}</div>
            ${o.available < o.quantity ? `<div class="notice">Only ${o.available} in stock — can’t be fulfilled</div>` : ""}
          </div>
          <div class="oc-actions">
            <button class="btn btn-sm btn-primary" data-action="process-one" data-id="${esc(o.id)}">Process</button>
            <button class="btn btn-sm btn-danger" data-action="cancel-order" data-id="${esc(o.id)}">Cancel</button>
          </div>
        </div>`).join("")}</div>`
    : `<div class="empty">No pending orders.</div>`;

  const hist = orders.data.history.slice().reverse();
  const histRows = hist.length
    ? hist.map(o => {
        let cls = "info";
        if (o.status === "Processed") cls = "ok";
        else if (o.status.indexOf("Rejected") === 0) cls = "danger";
        else if (o.status === "Pending") cls = "low";
        return `<tr>
          <td class="mono strong">${esc(o.id)}</td>
          <td class="dim">${esc(o.customerId)}</td>
          <td class="dim">${esc(o.productId)}</td>
          <td class="num mono">${o.quantity}</td>
          <td class="num mono strong">${money(o.amount)}</td>
          <td class="dim">${esc(o.warehouseId)}</td>
          <td><span class="badge ${cls}">${esc(o.status)}</span></td>
        </tr>`;
      }).join("")
    : `<tr><td colspan="7"><div class="empty">No orders yet.</div></td></tr>`;

  $("#view").innerHTML = `
    <div class="stack">
      <div class="card">
        <div class="card-head"><h2>Create order</h2><span class="hint">Active warehouse</span></div>
        ${createCard}
      </div>

      <div class="card">
        <div class="card-head">
          <h2>Pending orders</h2>
          <div style="display:flex;gap:8px">
            <button class="btn btn-sm btn-ghost" data-action="process-order" ${pending.length ? "" : "disabled"}>Process next</button>
            <button class="btn btn-sm btn-primary" data-action="process-all" ${pending.length ? "" : "disabled"}>Process all</button>
          </div>
        </div>
        ${queueHtml}
      </div>

      <div class="card">
        <div class="card-head"><h2>Order history</h2><span class="hint">${hist.length} records</span></div>
        <div class="table-wrap"><table>
          <thead><tr><th>Order</th><th>Customer</th><th>Product</th><th class="num">Qty</th><th class="num">Total</th><th>Warehouse</th><th>Status</th></tr></thead>
          <tbody>${histRows}</tbody>
        </table></div>
      </div>
    </div>`;

  const form = $('form[data-form="create-order"]');
  if (form) {
    const psel = form.querySelector('[name="productId"]');
    const qty = form.querySelector('[name="quantity"]');
    const out = $("#order-total");
    const update = () => {
      out.textContent = money((priceOf[psel.value] || 0) * (Number(qty.value) || 0));
    };
    psel.addEventListener("change", update);
    qty.addEventListener("input", update);
    update();
  }
}

/* ------------------------------ warehouses ------------------------------ */
async function viewWarehouses() {
  const { data: list } = await api("/warehouses");
  const rows = list.map(w => `
    <tr>
      <td class="mono strong">${esc(w.id)}</td>
      <td>${esc(w.location)}</td>
      <td class="num mono dim">${w.capacity}</td>
      <td class="num mono">${w.stock}</td>
      <td class="num mono dim">${w.products}</td>
      <td>${w.active ? '<span class="badge ok">Active</span>' : '<span class="badge">Idle</span>'}</td>
      <td class="num">
        <div style="display:flex;gap:6px;justify-content:flex-end">
          ${w.active ? "" : `<button class="btn btn-sm btn-ghost" data-action="set-warehouse" data-id="${esc(w.id)}">Switch</button>`}
          <button class="btn btn-sm btn-danger" data-action="remove-warehouse" data-id="${esc(w.id)}">Remove</button>
        </div>
      </td>
    </tr>`).join("");

  $("#view").innerHTML = `
    <div class="card">
      <div class="card-head"><h2>Add warehouse</h2><span class="hint">New storage location</span></div>
      <form data-form="add-warehouse" class="form-grid">
        <label class="field"><span>Warehouse ID</span><input name="id" placeholder="WH-02" required></label>
        <label class="field"><span>Location</span><input name="location" placeholder="North Depot" required></label>
        <label class="field"><span>Capacity (units)</span><input name="capacity" type="number" min="1" value="1000" required></label>
        <div class="field"><span>&nbsp;</span><button class="btn btn-primary" type="submit">Add warehouse</button></div>
      </form>
    </div>

    <div class="card mt">
      <div class="card-head"><h2>All warehouses</h2><span class="hint">${list.length} total</span></div>
      <div class="table-wrap"><table>
        <thead><tr><th>ID</th><th>Location</th><th class="num">Capacity</th><th class="num">Stock</th><th class="num">Products</th><th>Status</th><th class="num">Actions</th></tr></thead>
        <tbody>${rows}</tbody>
      </table></div>
    </div>`;
}

/* ------------------------------ customers ------------------------------- */
async function viewCustomers() {
  const { data: list } = await api("/customers");
  $("#view").innerHTML = `
    <div class="toolbar">
      <div class="search grow">
        <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/></svg>
        <input placeholder="Search customers…" data-search-input>
      </div>
      <button class="btn btn-primary" data-action="add-customer">+ Add customer</button>
    </div>
    <div class="card">
      <div class="table-wrap"><table>
        <thead><tr><th>ID</th><th>Name</th><th>Phone</th></tr></thead>
        <tbody>${personRows(list)}</tbody>
      </table></div>
    </div>`;
}

/* ------------------------------ suppliers ------------------------------- */
async function viewSuppliers() {
  const { data: list } = await api("/suppliers");
  $("#view").innerHTML = `
    <div class="toolbar">
      <div class="search grow">
        <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/></svg>
        <input placeholder="Search suppliers…" data-search-input>
      </div>
      <button class="btn btn-primary" data-action="add-supplier">+ Add supplier</button>
    </div>
    <div class="card">
      <div class="table-wrap"><table>
        <thead><tr><th>ID</th><th>Name</th><th>Phone</th></tr></thead>
        <tbody>${personRows(list)}</tbody>
      </table></div>
    </div>`;
}

/* ------------------------------ audit ----------------------------------- */
async function viewAudit() {
  const { data: entries } = await api("/audit");
  const list = entries.length
    ? entries.map((a, i) => `
        <div class="audit-row"><span class="audit-dot"></span><span>${esc(a)}</span>
          <span class="when">${i === 0 ? "latest" : "#" + (i + 1)}</span></div>`).join("")
    : `<div class="empty">No audit activity yet.</div>`;

  $("#view").innerHTML = `
    <div class="toolbar">
      <span class="muted grow">Each action is recorded. “Remove latest” deletes the most recent entry.</span>
      <button class="btn btn-danger" data-action="pop-audit" ${entries.length ? "" : "disabled"}>Remove latest</button>
    </div>
    <div class="card">
      <div class="card-head"><h2>Activity log</h2><span class="hint">${entries.length} entries</span></div>
      <div class="audit-list">${list}</div>
    </div>`;
}

/* ------------------------------ auth UI --------------------------------- */
function setUser(name) {
  if (name) localStorage.setItem("wims_user", name);
}
function showLogin() { $("#login").hidden = false; $("#app").hidden = true; }
function showApp()   { $("#login").hidden = true;  $("#app").hidden = false; }
function signOutLocal() {
  AUTH_TOKEN = "";
  localStorage.removeItem("wims_token");
  showLogin();
}

async function doLogin(user, pass) {
  const r = await api("/login", "POST", { username: user, password: pass });
  AUTH_TOKEN = r.data.token;
  localStorage.setItem("wims_token", AUTH_TOKEN);
  setUser(r.data.user);
  showApp();
  toast("Welcome, " + r.data.user, "ok");
  await navigate("dashboard");
}
async function doLogout() {
  try { await api("/logout", "POST"); } catch (e) {}
  signOutLocal();
  toast("Signed out.", "info");
}

/* ------------------------------ events ---------------------------------- */
function attachEvents() {
  $("#nav").addEventListener("click", (e) => {
    const btn = e.target.closest(".nav-item");
    if (btn) navigate(btn.dataset.view);
  });

  document.addEventListener("click", (e) => {
    const el = e.target.closest("[data-action]");
    if (!el) return;
    handleAction(el.dataset.action, el).catch(err => toast(err.message, "error"));
  });

  $("#modal-root").addEventListener("click", (e) => {
    if (e.target.classList.contains("overlay") || e.target.closest("[data-close]")) closeModal();
  });

  $("#view").addEventListener("input", (e) => {
    const input = e.target.closest("[data-search-input]");
    if (input) applyFilter(input);
  });
  $("#view").addEventListener("submit", (e) => {
    const form = e.target.closest("form[data-form]");
    if (!form) return;
    e.preventDefault();
    handleForm(form.dataset.form, form).catch(err => toast(err.message, "error"));
  });

  $("#theme-btn").addEventListener("click", toggleTheme);

  $("#wh-select").addEventListener("change", async (e) => {
    try {
      const r = await api("/warehouses/active", "POST", { id: e.target.value });
      toast(r.message, "ok");
      await refresh();
    } catch (err) {
      toast(err.message, "error");
      await loadWarehousePicker();
    }
  });

  $("#login-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    const errEl = $("#login-error");
    errEl.textContent = "";
    const btn = e.target.querySelector('button[type="submit"]');
    btn.disabled = true;
    try {
      await doLogin($("#login-user").value.trim(), $("#login-pass").value);
    } catch (err) {
      errEl.textContent = err.message;
    } finally {
      btn.disabled = false;
    }
  });
}

function applyFilter(input) {
  const q = input.value.toLowerCase().trim();
  const scope = input.closest(".view") || document;
  $$("[data-row]", scope).forEach(row => {
    row.style.display = row.dataset.search.includes(q) ? "" : "none";
  });
}

async function handleForm(kind, form) {
  const data = Object.fromEntries(new FormData(form).entries());
  if (kind === "create-order") {
    data.quantity = Number(data.quantity);
    const r = await api("/orders", "POST", data);
    toast(r.message, "ok");
    form.reset();
    await refresh();
  } else if (kind === "add-warehouse") {
    data.capacity = Number(data.capacity);
    const r = await api("/warehouses", "POST", data);
    toast(r.message, "ok");
    form.reset();
    await refresh();
  }
}

async function handleAction(action, el) {
  const id = el.dataset.id;

  switch (action) {
    case "save": {
      const r = await api("/save", "POST");
      toast(r.message, "ok");
      break;
    }
    case "logout":
      await doLogout();
      break;

    case "add-product":
      openFormModal({
        title: "Add product",
        submitLabel: "Add product",
        fields: [
          { name: "id", label: "Product ID", placeholder: "P001", required: true },
          { name: "name", label: "Name", placeholder: "Steel Bolt", required: true },
          { name: "category", label: "Category", placeholder: "Hardware", required: true },
          { name: "price", label: "Price (₹)", type: "number", step: "0.01", min: 0, value: 0, required: true },
          { name: "quantity", label: "Initial stock", type: "number", min: 0, value: 0, required: true },
          { name: "reorderLevel", label: "Reorder level", type: "number", min: 0, value: 5, required: true },
        ],
        onSubmit: async (v) => { const r = await api("/products", "POST", v); toast(r.message, "ok"); await refresh(); },
      });
      break;

    case "restock":
      openFormModal({
        title: "Restock product " + id,
        submitLabel: "Add stock",
        fields: [{ name: "amount", label: "Quantity to add", type: "number", min: 1, value: 1, required: true }],
        onSubmit: async (v) => { const r = await api("/products/restock", "POST", { id, amount: v.amount }); toast(r.message, "ok"); await refresh(); },
      });
      break;

    case "price":
      openFormModal({
        title: "Change price of " + id,
        submitLabel: "Update price",
        fields: [{ name: "price", label: "New price (₹)", type: "number", step: "0.01", min: 0, required: true }],
        onSubmit: async (v) => { const r = await api("/products/price", "POST", { id, price: v.price }); toast(r.message, "ok"); await refresh(); },
      });
      break;

    case "delete-product":
      openConfirm({
        title: "Delete product",
        message: `Remove “${el.dataset.name}” (${id}) from this warehouse? This cannot be undone.`,
        confirmLabel: "Delete",
        onConfirm: async () => { const r = await api("/products/remove", "POST", { id }); toast(r.message, "ok"); await refresh(); },
      });
      break;

    case "process-order": {
      const r = await api("/orders/process", "POST");
      toast(`Order ${r.data.id} → ${r.data.status}`, r.data.status === "Processed" ? "ok" : "info");
      await refresh();
      break;
    }
    case "process-one": {
      const r = await api("/orders/process-one", "POST", { id });
      toast(`Order ${r.data.id} → ${r.data.status}`, r.data.status === "Processed" ? "ok" : "info");
      await refresh();
      break;
    }
    case "process-all": {
      const r = await api("/orders/process-all", "POST");
      toast(`Processed ${r.data.count} order${r.data.count === 1 ? "" : "s"}.`, "ok");
      await refresh();
      break;
    }
    case "cancel-order":
      openConfirm({
        title: "Cancel order",
        message: `Cancel order ${id}? It will be removed from the queue.`,
        confirmLabel: "Cancel order",
        onConfirm: async () => { const r = await api("/orders/cancel", "POST", { id }); toast(r.message, "ok"); await refresh(); },
      });
      break;

    case "add-customer":
      openFormModal({
        title: "Add customer",
        submitLabel: "Add customer",
        fields: [
          { name: "id", label: "Customer ID", placeholder: "C001", required: true },
          { name: "name", label: "Name", placeholder: "Rahul Sharma", required: true },
          { name: "phone", label: "Phone", placeholder: "98765 43210", required: true },
        ],
        onSubmit: async (v) => { const r = await api("/customers", "POST", v); toast(r.message, "ok"); await refresh(); },
      });
      break;

    case "add-supplier":
      openFormModal({
        title: "Add supplier",
        submitLabel: "Add supplier",
        fields: [
          { name: "id", label: "Supplier ID", placeholder: "S001", required: true },
          { name: "name", label: "Name", placeholder: "Acme Supplies", required: true },
          { name: "phone", label: "Phone", placeholder: "98765 43210", required: true },
        ],
        onSubmit: async (v) => { const r = await api("/suppliers", "POST", v); toast(r.message, "ok"); await refresh(); },
      });
      break;

    case "set-warehouse": {
      const r = await api("/warehouses/active", "POST", { id });
      toast(r.message, "ok");
      await refresh();
      break;
    }
    case "remove-warehouse":
      openConfirm({
        title: "Remove warehouse",
        message: `Remove warehouse ${id}? It must be empty first.`,
        confirmLabel: "Remove",
        onConfirm: async () => { const r = await api("/warehouses/remove", "POST", { id }); toast(r.message, "ok"); await refresh(); },
      });
      break;

    case "pop-audit": {
      const r = await api("/audit/pop", "POST");
      toast(r.message, "ok");
      await refresh();
      break;
    }
  }
}

/* ------------------------------ boot ------------------------------------ */
async function boot() {
  applyTheme();
  attachEvents();
  setUser(localStorage.getItem("wims_user"));

  if (AUTH_TOKEN) {
    try {
      await api("/state");
      showApp();
      await navigate("dashboard");
      return;
    } catch (e) { }
  }
  showLogin();
}

boot();
