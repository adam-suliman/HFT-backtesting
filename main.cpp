#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

// Data structures for order book levels and snapshots
struct Level {
    double price;
    double volume;
};

struct OrderBookSnapshot {
    long long timestamp;
    std::vector<Level> asks;
    std::vector<Level> bids;
    OrderBookSnapshot(int levels = 0) {
        timestamp = 0;
        asks.resize(levels);
        bids.resize(levels);
    }
};

struct Trade {
    long long timestamp;
    std::string side;  // "buy" or "sell"
    double price;
    double volume;
};

// Structure to hold simulation results and stats
struct SimulationStats {
    int tradesProcessed;
    int buyTradeCount;
    int sellTradeCount;
    double totalBuyVolume;
    double totalSellVolume;
    double averageBuyPrice;
    double averageSellPrice;
    double finalPosition;
    double netPnL;
};

// CSV Parser for order book snapshots and trades
class CSVParser {
public:
    static bool parseOrderBook(const std::string& filename, std::vector<OrderBookSnapshot>& snapshots) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;
        std::string header;
        if (!std::getline(file, header)) return false;
        // Remove UTF-8 BOM if present
        if (header.size() >= 3 &&
            (unsigned char)header[0] == 0xEF &&
            (unsigned char)header[1] == 0xBB &&
            (unsigned char)header[2] == 0xBF) {
            header.erase(0, 3);
        }
        // Determine number of order book levels from header (count "asks[" occurrences)
        int levelCount = 0;
        size_t pos = 0;
        while ((pos = header.find("asks[", pos)) != std::string::npos) {
            levelCount++;
            pos += 5;
        }
        if (levelCount == 0) levelCount = 25;  // default to 25 levels if not found
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string cell;
            OrderBookSnapshot snapshot(levelCount);
            int col = 0;
            while (std::getline(ss, cell, ',')) {
                if (col == 0) {
                    // Skip index column
                }
                else if (col == 1) {
                    snapshot.timestamp = std::stoll(cell);
                }
                else {
                    // Determine which ask/bid level and field this column corresponds to
                    int offset = col - 2;
                    int lvl = offset / 4;
                    int fieldPos = offset % 4;
                    double value = std::stod(cell);
                    if (fieldPos == 0) {
                        snapshot.asks[lvl].price = value;
                    }
                    else if (fieldPos == 1) {
                        snapshot.asks[lvl].volume = value;
                    }
                    else if (fieldPos == 2) {
                        snapshot.bids[lvl].price = value;
                    }
                    else if (fieldPos == 3) {
                        snapshot.bids[lvl].volume = value;
                    }
                }
                col++;
            }
            snapshots.push_back(std::move(snapshot));
        }
        // Sort snapshots by time (they should already be in order)
        std::sort(snapshots.begin(), snapshots.end(), [](const OrderBookSnapshot& a, const OrderBookSnapshot& b) {
            return a.timestamp < b.timestamp;
            });
        return true;
    }

    static bool parseTrades(const std::string& filename, std::vector<Trade>& trades) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;
        std::string header;
        if (!std::getline(file, header)) return false;
        if (header.size() >= 3 &&
            (unsigned char)header[0] == 0xEF &&
            (unsigned char)header[1] == 0xBB &&
            (unsigned char)header[2] == 0xBF) {
            header.erase(0, 3);
        }
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string cell;
            Trade trade;
            int col = 0;
            while (std::getline(ss, cell, ',')) {
                if (col == 0) {
                    // Skip index column
                }
                else if (col == 1) {
                    trade.timestamp = std::stoll(cell);
                }
                else if (col == 2) {
                    trade.side = cell;  // "buy" or "sell"
                }
                else if (col == 3) {
                    trade.price = std::stod(cell);
                }
                else if (col == 4) {
                    trade.volume = std::stod(cell);
                }
                col++;
            }
            trades.push_back(trade);
        }
        std::sort(trades.begin(), trades.end(), [](const Trade& a, const Trade& b) {
            return a.timestamp < b.timestamp;
            });
        return true;
    }
};

// Backtest Engine: simulates the execution of trades against the order book
class BacktestEngine {
public:
    BacktestEngine(const std::vector<OrderBookSnapshot>& snapshots)
        : snapshots(snapshots), snapIndex(0) {
        if (!snapshots.empty()) {
            currentBook = snapshots[0];  // start with first snapshot
        }
    }

    SimulationStats run(const std::vector<Trade>& trades, std::ostream& out) {
        SimulationStats stats{};
        stats.tradesProcessed = 0;
        stats.buyTradeCount = 0;
        stats.sellTradeCount = 0;
        stats.totalBuyVolume = 0.0;
        stats.totalSellVolume = 0.0;
        stats.averageBuyPrice = 0.0;
        stats.averageSellPrice = 0.0;
        stats.finalPosition = 0.0;
        stats.netPnL = 0.0;

        double cash = 0.0;
        double position = 0.0;
        double buyCostSum = 0.0;
        double sellRevenueSum = 0.0;

        out << "TradeID\tTime\tSide\tVolumeFilled\tAvgFillPrice\n";
        for (size_t i = 0; i < trades.size(); ++i) {
            const Trade& trade = trades[i];
            // Advance current order book to the latest snapshot at or before this trade's time
            while (snapIndex < snapshots.size() && snapshots[snapIndex].timestamp <= trade.timestamp) {
                currentBook = snapshots[snapIndex];
                snapIndex++;
            }
            if (snapIndex == 0 && snapshots[0].timestamp > trade.timestamp) {
                // No snapshot available yet for this early trade timestamp
                out << "No order book snapshot for trade at time " << trade.timestamp << ". Skipping trade.\n";
                continue;
            }

            // Execute the trade as a market order
            double remainingVol = trade.volume;
            double filledVol = 0.0;
            double costAccumulated = 0.0;
            if (trade.side == "buy") {
                // Market buy: consume ask levels
                for (size_t lvl = 0; lvl < currentBook.asks.size() && remainingVol > 1e-12; ++lvl) {
                    double lvlPrice = currentBook.asks[lvl].price;
                    double lvlVol = currentBook.asks[lvl].volume;
                    if (lvlVol <= 0) continue;
                    if (lvlVol >= remainingVol) {
                        // Fill the entire remaining volume at this price
                        costAccumulated += remainingVol * lvlPrice;
                        filledVol += remainingVol;
                        currentBook.asks[lvl].volume = lvlVol - remainingVol;
                        remainingVol = 0;
                        break;
                    }
                    else {
                        // Take all volume at this level and continue to next level
                        costAccumulated += lvlVol * lvlPrice;
                        filledVol += lvlVol;
                        remainingVol -= lvlVol;
                        currentBook.asks[lvl].volume = 0;
                    }
                }
            }
            else if (trade.side == "sell") {
                // Market sell: consume bid levels
                for (size_t lvl = 0; lvl < currentBook.bids.size() && remainingVol > 1e-12; ++lvl) {
                    double lvlPrice = currentBook.bids[lvl].price;
                    double lvlVol = currentBook.bids[lvl].volume;
                    if (lvlVol <= 0) continue;
                    if (lvlVol >= remainingVol) {
                        costAccumulated += remainingVol * lvlPrice;
                        filledVol += remainingVol;
                        currentBook.bids[lvl].volume = lvlVol - remainingVol;
                        remainingVol = 0;
                        break;
                    }
                    else {
                        costAccumulated += lvlVol * lvlPrice;
                        filledVol += lvlVol;
                        remainingVol -= lvlVol;
                        currentBook.bids[lvl].volume = 0;
                    }
                }
            }
            else {
                out << "Unknown trade side: " << trade.side << "\n";
                continue;
            }
            if (remainingVol > 1e-9) {
                out << "Trade at time " << trade.timestamp
                    << " not fully filled (unfilled: " << remainingVol << ")\n";
            }

            double avgFillPrice = (filledVol > 1e-12) ? (costAccumulated / filledVol) : 0.0;
            // Update portfolio cash/position and stats
            if (trade.side == "buy") {
                position += filledVol;
                cash -= costAccumulated;
                stats.totalBuyVolume += filledVol;
                buyCostSum += costAccumulated;
                stats.buyTradeCount++;
            }
            else if (trade.side == "sell") {
                position -= filledVol;
                cash += costAccumulated;
                stats.totalSellVolume += filledVol;
                sellRevenueSum += costAccumulated;
                stats.sellTradeCount++;
            }
            stats.tradesProcessed++;
            out << i << "\t" << trade.timestamp << "\t" << trade.side
                << "\t" << filledVol << "\t" << avgFillPrice << "\n";
        }

        // Mark-to-market any open position at final snapshot prices
        double bestBidPrice = currentBook.bids.empty() ? 0.0 : currentBook.bids[0].price;
        double bestAskPrice = currentBook.asks.empty() ? 0.0 : currentBook.asks[0].price;
        double markValue = 0.0;
        if (position > 1e-9) {
            markValue = position * bestBidPrice;  // value long position at best bid
        }
        else if (position < -1e-9) {
            markValue = position * bestAskPrice;  // cost to cover short position at best ask (negative value)
        }
        stats.finalPosition = position;
        stats.netPnL = cash + markValue;
        // Compute average executed prices for buys and sells
        if (stats.totalBuyVolume > 1e-9) {
            stats.averageBuyPrice = buyCostSum / stats.totalBuyVolume;
        }
        if (stats.totalSellVolume > 1e-9) {
            stats.averageSellPrice = sellRevenueSum / stats.totalSellVolume;
        }
        return stats;
    }

private:
    const std::vector<OrderBookSnapshot>& snapshots;
    OrderBookSnapshot currentBook;
    size_t snapIndex;
};

int main() {
    std::string lobFile = "lob.csv";
    std::string tradesFile = "trades.csv";
    std::string outputFile = "backtest_output.txt";
    std::ofstream out(outputFile);
    if (!out.is_open()) {
        return 1;
    }
    std::vector<OrderBookSnapshot> snapshots;
    std::vector<Trade> trades;
    if (!CSVParser::parseOrderBook(lobFile, snapshots)) {
        out << "Error: failed to load order book data.\n";
        return 1;
    }
    if (!CSVParser::parseTrades(tradesFile, trades)) {
        out << "Error: failed to load trades data.\n";
        return 1;
    }
    BacktestEngine engine(snapshots);
    out << "Simulating " << trades.size() << " trades against historical order book...\n";
    SimulationStats stats = engine.run(trades, out);

    // Print summary statistics
    out << "\n--- Simulation Summary ---\n";
    out << "Order book snapshots: " << snapshots.size() << "\n";
    out << "Trades processed: " << stats.tradesProcessed
        << " (out of " << trades.size() << ")\n";
    out << "Total volume traded: "
        << stats.totalBuyVolume + stats.totalSellVolume
        << " (buys: " << stats.totalBuyVolume << ", sells: " << stats.totalSellVolume << ")\n";
    out << "Average buy price: " << stats.averageBuyPrice
        << ", Average sell price: " << stats.averageSellPrice << "\n";
    out << "Final position: " << stats.finalPosition;
    if (stats.finalPosition > 1e-9) {
        out << " (long)\n";
        double marketVal = stats.finalPosition * (snapshots.back().bids.empty()
            ? 0.0 : snapshots.back().bids[0].price);
        out << "Market value of open position: " << marketVal << "\n";
    }
    else if (stats.finalPosition < -1e-9) {
        out << " (short)\n";
        double marketVal = stats.finalPosition * (snapshots.back().asks.empty()
            ? 0.0 : snapshots.back().asks[0].price);
        out << "Market value of open position: " << marketVal
            << " (negative = liability)\n";
    }
    else {
        out << "\n";
    }
    out << "Net P&L: " << stats.netPnL << "\n";
}
