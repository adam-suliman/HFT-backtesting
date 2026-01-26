# HFT Backtester — User & Developer Documentation

## 1) What this tool does
This backtesting tool **replays historical order book snapshots** and **executes a stream of strategy trades** (treated as market orders) against those snapshots. It then **reports fills** and **computes P&L** (including mark‑to‑market on any residual position).

It’s intentionally simple and fast to help you validate basic execution assumptions and strategy behavior on recorded market depth.

---

## 2) Quickstart
1. **Place files** in the working directory:
   - `lob.csv` — order book snapshots
   - `trades.csv` — strategy trades to execute
2. **Build** (GCC/Clang, C++11+):
   ```bash
   g++ -O2 -std=c++11 -o backtester main.cpp
   ```
3. **Run**:
   ```bash
   ./backtester
   ```
4. **Read output file**: `backtest_output.txt` contains the per-trade fill log followed by a summary (volume, average prices, position, net P&L).

> If your CSV filenames differ, edit the two variables near the top of `main()` (`lobFile`, `tradesFile`) and rebuild. To change the output file name, edit `outputFile`.

---

## 3) Data formats
### 3.1 Order book CSV (`lob.csv`)
**Expected header pattern (example with 3 depth levels):**
```
index,timestamp,asks[0].price,asks[0].volume,bids[0].price,bids[0].volume,asks[1].price,asks[1].volume,bids[1].price,bids[1].volume,asks[2].price,asks[2].volume,bids[2].price,bids[2].volume
```
- **`timestamp`**: integer (e.g., micro/nanoseconds since epoch or since session start).
- **Asks**: ascending by price.
- **Bids**: descending by price.
- The tool **infers depth** by counting `asks[` occurrences in the header (default 25 if not found).
- **Rows**: one snapshot per row. All numeric fields are parsed as `double` (price, volume).

> If your actual CSV nests asks/bids differently (e.g., JSON blobs or semicolon-delimited lists), convert them to the columnar pattern above **or** adapt the parsing in `CSVParser::parseOrderBook`.

### 3.2 Trades CSV (`trades.csv`)
**Expected header:**
```
index,timestamp,side,price,volume
```
- **`side`**: `buy` or `sell` (lowercase).
- **`price`**: not used for market execution, but kept for auditing (e.g., original intent).
- **`volume`**: trade size (double).

> If your column is named `amount` instead of `volume`, either rename in CSV or adjust parsing at `CSVParser::parseTrades` (column index 4).

### 3.3 BOM & sorting
- UTF‑8 BOM on the first line is detected and removed.
- Files are sorted by `timestamp` after load; order of equal timestamps is preserved as read.

---

## 4) Simulation semantics (very important)
- **Event ordering**: For each trade `T`, the engine **advances** the order book to the **latest snapshot with `timestamp <= T.timestamp`** and uses that book to execute `T`. If **no snapshot exists yet** (book starts later), `T` is **skipped** with a warning.
- **Execution model**: Trades are treated as **market orders**:
  - `buy` consumes **asks** starting from best ask upward.
  - `sell` consumes **bids** starting from best bid downward.
  - Supports **partial fills across levels** until the order is filled or all levels are exhausted.
- **Book mutation**: Fills **deplete volumes** of the current in‑memory snapshot. When time advances to a **later snapshot**, the book is **overwritten** by that snapshot (i.e., external market updates replace any local depletion).
- **Costs/fees**: Not modeled. You cross the spread on market orders, which is visible in average buy vs sell prices.
- **Latency/queue**: Not modeled (zero‑latency, no queue position). See §10 for limitations and extensions.

---

## 5) Outputs
### 5.1 Trade fill log
Written to `backtest_output.txt`:
```
TradeID  Time            Side  VolumeFilled  AvgFillPrice
1        1722470402982…  sell      1633        0.0110435
...
```
- **`VolumeFilled`** may be < requested volume if the book lacks depth at that moment.
- **`AvgFillPrice`** is volume‑weighted across levels.

### 5.2 Summary
- Snapshots processed
- Trades processed (and skipped)
- Total traded volume (buys / sells)
- Average buy/sell prices
- Final position (positive=long, negative=short)
- Net P&L = **cash** + **mark‑to‑market** of open position
  - Long valued at **best bid**
  - Short valued at **best ask**

All warnings/errors and the simulation log are written to `backtest_output.txt`; nothing is printed to the terminal during normal runs.

---

## 6) Build & run
### 6.1 Requirements
- **Compiler**: GCC ≥ 5, Clang ≥ 6, MSVC ≥ 2017 (C++11+)
- **OS**: Linux / macOS / Windows

### 6.2 Compile
```bash
g++ -O2 -std=c++11 -o backtester main.cpp
```
- Use `-O2` (or `-O3`) for performance.
- Optional sanitizers while debugging:
  ```bash
  g++ -g -fsanitize=address,undefined -std=c++11 -o backtester_dbg main.cpp
  ```

### 6.3 Run
```bash
./backtester
```
- Edit `lobFile` / `tradesFile` / `outputFile` in `main()` if filenames differ.
- Output is written to `backtest_output.txt` in the working directory.

---

## 7) Code architecture
```
main.cpp
├─ struct Level { double price, volume; }
├─ struct OrderBookSnapshot { long long timestamp; vector<Level> asks, bids; }
├─ struct Trade { long long timestamp; string side; double price, volume; }
├─ struct SimulationStats { /* counts, averages, position, PnL */ }
│
├─ class CSVParser
│   ├─ static bool parseOrderBook(file, vector<OrderBookSnapshot>&)
│   └─ static bool parseTrades(file, vector<Trade>&)
│
└─ class BacktestEngine
    ├─ BacktestEngine(const vector<OrderBookSnapshot>& snapshots)
    └─ SimulationStats run(const vector<Trade>& trades, ostream& out)
```
- **CSVParser**: lightweight, column‑based parsing using `std::getline` and `std::stod`.
- **BacktestEngine**: merges time streams (snapshots and trades) and performs matching.

---

## 8) API reference (minimal)
### 8.1 `CSVParser`
- `parseOrderBook(const std::string&, std::vector<OrderBookSnapshot>&) -> bool`
  - Loads `lob.csv`. Infers depth by counting `asks[` in header. Populates vector sorted by time.
- `parseTrades(const std::string&, std::vector<Trade>&) -> bool`
  - Loads `trades.csv`. Populates vector sorted by time.

### 8.2 `BacktestEngine`
- `BacktestEngine(const std::vector<OrderBookSnapshot>& snapshots)`
  - Initializes current book to the first snapshot; keeps an index to advance over time.
- `SimulationStats run(const std::vector<Trade>& trades, std::ostream& out)`
  - Executes trades, writes per-trade log and summary to the provided stream, returns summary stats.

### 8.3 `SimulationStats`
- Fields:
  - `int tradesProcessed, buyTradeCount, sellTradeCount;`
  - `double totalBuyVolume, totalSellVolume;`
  - `double averageBuyPrice, averageSellPrice;`
  - `double finalPosition, netPnL;`

---

## 9) Testing
- **Unit tests** (recommended): add a test translation unit with [Catch2] or [doctest].
  - Parser: feed tiny CSV strings, assert parsed values.
  - Matching: hand‑craft a snapshot with two ask levels and verify fills for a buy spanning both.
- **Smoke test**: Make tiny CSVs (2 snapshots, 2 trades) and check the output file log matches hand calculation.

---

## 10) Known limitations & caveats
1. **No latency/queue modeling**: Orders execute on the snapshot time with zero delay; passive/queue priority isn’t modeled.
2. **No passive limit orders**: Only market‑style execution is implemented. Resting orders and later fills are not simulated.
3. **Book overwrite on new snapshots**: Depletions from earlier trades are not carried into later snapshots (market data is treated as ground truth).
4. **No fees/slippage model**: Only spread cost is implicit for market orders (buy@ask, sell@bid).
5. **Precision**: Prices/volumes are `double`. If you require exact cents/ticks, switch to fixed‑point integers.
6. **CSV schema dependency**: Column names follow `asks[i].price/volume` & `bids[i].price/volume`. Adjust parser if your schema differs.

---

## 11) Performance tips
- Compile with `-O2` or `-O3`.
- Avoid logging every event when running at scale.
- If CSVs are huge, consider streaming (process lines on‑the‑fly) or swap in a high‑performance CSV library.
- Pre‑`reserve()` vectors when possible and reuse objects in hot paths.

---

## 12) Extending the tool
- **CLI arguments**: Parse `--lob path`, `--trades path` instead of hardcoding.
- **Fees & spreads**: Add maker/taker fees; show fee‑adjusted P&L.
- **Passive orders**: Keep a book of resting strategy orders; fill when the market trades through that level.
- **Latency**: Delay strategy orders by X µs; optionally **miss** the triggering snapshot.
- **Queue position**: Conservative assumption (fill only if traded volume at your price since placement exceeds pre‑existing queue).
- **Impact modeling**: Apply your fills to subsequent snapshots (risk: divergence from recorded data).

---

## 14) Glossary
- **Snapshot**: State of the order book (bids/asks) at a specific time.
- **Best bid/ask**: Highest bid price / lowest ask price available.
- **Market order**: Order that executes immediately against the book.
- **VWAP**: Volume‑weighted average price of execution.
- **Mark‑to‑market**: Valuing open position at current market price.
