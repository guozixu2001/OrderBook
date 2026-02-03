#include <gtest/gtest.h>
#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook* ob;

    void SetUp() override {
        ob = new OrderBook("TEST_SYMBOL");
    }

    void TearDown() override {
        delete ob;
    }
};

// ==========================================
// ==========================================

TEST_F(OrderBookTest, AddOrderUpdatesBBO) {
    EXPECT_EQ(ob->getBBO()->bid_price, 0);
    EXPECT_EQ(ob->getBBO()->ask_price, 0);

    ob->addOrder(1, 100, 10, Side::BUY);
    
    EXPECT_EQ(ob->getBBO()->bid_price, 100);
    EXPECT_EQ(ob->getBBO()->bid_qty, 10);

    ob->addOrder(2, 101, 5, Side::BUY);

    EXPECT_EQ(ob->getBBO()->bid_price, 101);
    EXPECT_EQ(ob->getBBO()->bid_qty, 5);
}

TEST_F(OrderBookTest, DeleteOrderUpdatesBBO) {
    ob->addOrder(1, 100, 10, Side::SELL);
    ob->addOrder(2, 102, 20, Side::SELL);

    EXPECT_EQ(ob->getBBO()->ask_price, 100);

    ob->deleteOrder(1, Side::SELL);

    EXPECT_EQ(ob->getBBO()->ask_price, 102);
    EXPECT_EQ(ob->getBBO()->ask_qty, 20);
}

TEST_F(OrderBookTest, ModifyOrderLogic) {
    ob->addOrder(1, 100, 10, Side::BUY);
    
    ob->modifyOrder(1, 100, 20, Side::BUY);
    EXPECT_EQ(ob->getBBO()->bid_qty, 20);

    ob->modifyOrder(1, 105, 20, Side::BUY);
    EXPECT_EQ(ob->getBBO()->bid_price, 105);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, SignalMidPriceAndSpread) {
    // Bid: 100
    // Ask: 110
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 110, 10, Side::SELL);

    // Mid Price = (100 + 110) / 2 = 105.0
    EXPECT_DOUBLE_EQ(ob->getMidPrice(), 105.0);

    // Spread = 110 - 100 = 10
    EXPECT_EQ(ob->getSpread(), 10);
}

TEST_F(OrderBookTest, SignalImbalance) {
    // Bid Qty: 30 (10 + 20)
    // Ask Qty: 10
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);
    ob->addOrder(3, 110, 10, Side::SELL);

    // Imbalance Formula: (BidQty - AskQty) / (BidQty + AskQty)
    // (30 - 10) / (30 + 10) = 20 / 40 = 0.5
    EXPECT_DOUBLE_EQ(ob->getImbalance(5), 0.5);
}

TEST_F(OrderBookTest, SignalMicroPrice) {
    // Macro Price (Volume Weighted Mid Price)
    // Bid: 100 (Qty 10), Ask: 110 (Qty 30)
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 110, 30, Side::SELL);

    // Formula: (AskPrice * BidQty + BidPrice * AskQty) / (BidQty + AskQty)
    // (110 * 10 + 100 * 30) / (10 + 30)
    // (1100 + 3000) / 40 = 4100 / 40 = 102.5
    // Wait, let's check code logic:
    // return (bbo_.ask_price * bid_weight + bbo_.bid_price * ask_weight) ...
    // (110 * 10 + 100 * 30) / 40 = 102.5
    
    EXPECT_DOUBLE_EQ(ob->getMacroPrice(), 102.5);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, TradeProcessingAndVWAP) {
    uint64_t time1 = 1672574400000000000ULL; 
    
    ob->addOrder(10, 100, 100, Side::BUY);
    
    ob->processTrade(10, 999, 100, 10, Side::BUY, time1);

    EXPECT_EQ(ob->getWindowVolume(), 10);
    // VWAP = (100 * 10) / 10 = 100
    EXPECT_EQ(ob->getVWAP(), 100);

    ob->addOrder(11, 110, 100, Side::SELL);
    ob->processTrade(11, 1000, 110, 10, Side::SELL, time1 + 1000); // +1us

    // Total Volume = 20
    EXPECT_EQ(ob->getWindowVolume(), 20);
    // Total Amount = 100*10 + 110*10 = 1000 + 1100 = 2100
    // VWAP = 2100 / 20 = 105
    EXPECT_EQ(ob->getVWAP(), 105);
}

TEST_F(OrderBookTest, ModifyOrderChangesBBO) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);

    EXPECT_EQ(ob->getBBO()->bid_price, 100);

    ob->modifyOrder(1, 98, 10, Side::BUY);

    EXPECT_EQ(ob->getBBO()->bid_price, 99);
    EXPECT_EQ(ob->getBBO()->bid_qty, 20);

    EXPECT_EQ(ob->getBidPrice(1), 98);
}

TEST_F(OrderBookTest, TradePartialFillLogic) {
    uint64_t t = 1000000000ULL; // 1 sec
    
    ob->addOrder(1, 100, 50, Side::SELL);
    
    ob->processTrade(1, 1001, 100, 20, Side::SELL, t);

    EXPECT_EQ(ob->getBBO()->ask_qty, 30);
    
    ob->processTrade(1, 1002, 100, 30, Side::SELL, t);

    EXPECT_EQ(ob->getBBO()->ask_qty, 0);
    EXPECT_EQ(ob->getBBO()->ask_price, 0);
}

TEST_F(OrderBookTest, CalculateBookPressure) {
    // Mid Price = 100
    
    // Bid: 99 (dist=1), Qty=10.   Pressure contribution = 10/1 = 10
    ob->addOrder(1, 99, 10, Side::BUY);
    
    // Ask: 101 (dist=1), Qty=5.   Pressure contribution = 5/1 = 5
    ob->addOrder(2, 101, 5, Side::SELL);

    // Ask: 102 (dist=2), Qty=20.  Pressure contribution = 20/2 = 10
    ob->addOrder(3, 102, 20, Side::SELL);

    // Bid Pressure = 10
    // Ask Pressure = 5 + 10 = 15
    // Total Pressure = 25
    // Result = (Bid - Ask) / Total = (10 - 15) / 25 = -0.2
    
    
    EXPECT_NEAR(ob->getBookPressure(5), -0.2, 0.0001);
}

TEST_F(OrderBookTest, VWAPLevelMapping) {
    uint64_t t = 1000000000ULL;
    
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 10, Side::BUY);
    ob->addOrder(3, 98, 10, Side::BUY);

    ob->addOrder(4, 102, 10, Side::SELL); // Level 0
    ob->addOrder(5, 103, 10, Side::SELL); // Level 1

    ob->addOrder(999, 99, 1000, Side::BUY); 
    ob->processTrade(999, 1, 99, 100, Side::BUY, t);
    
    EXPECT_EQ(ob->getVWAP(), 99);
    EXPECT_EQ(ob->getVWAPLevel(), 1); 

    ob->addOrder(888, 104, 20000, Side::SELL);
    ob->processTrade(888, 2, 104, 10000, Side::SELL, t);
    
    EXPECT_EQ(ob->getVWAP(), 103);
    
    EXPECT_EQ(ob->getVWAPLevel(), -1);
}


TEST_F(OrderBookTest, SlidingWindowEviction) {
    uint64_t t2_fmt = 20230101121001ULL; 
    uint64_t base_ns = 1672574400000000000ULL; 
    
    ob->addOrder(1, 100, 100, Side::BUY);
    ob->processTrade(1, 1, 100, 10, Side::BUY, base_ns);
    
    EXPECT_EQ(ob->getWindowVolume(), 10);
    EXPECT_EQ(ob->getPriceRange(), 0); 

    ob->addOrder(2, 110, 100, Side::BUY);
    ob->processTrade(2, 2, 110, 20, Side::BUY, base_ns + 300ULL * 1000000000ULL);
    
    EXPECT_EQ(ob->getWindowVolume(), 30); 
    EXPECT_EQ(ob->getPriceRange(), 10);   

    ob->evictExpiredTrades(t2_fmt);

    EXPECT_EQ(ob->getWindowVolume(), 20); 
    EXPECT_EQ(ob->getPriceRange(), 0);    
    EXPECT_EQ(ob->getVWAP(), 110);
}

TEST_F(OrderBookTest, MedianPriceCalculation) {
    uint64_t t = 1000000000ULL;
    
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->processTrade(1, 1, 100, 1, Side::BUY, t);

    ob->addOrder(2, 300, 10, Side::BUY);
    ob->processTrade(2, 2, 300, 1, Side::BUY, t);

    ob->addOrder(3, 200, 10, Side::BUY);
    ob->processTrade(3, 3, 200, 1, Side::BUY, t); 

    EXPECT_EQ(ob->getMedianPrice(), 200);

    ob->addOrder(4, 400, 10, Side::BUY);
    ob->processTrade(4, 4, 400, 1, Side::BUY, t);
    
    EXPECT_EQ(ob->getMedianPrice(), 250);
}



TEST_F(OrderBookTest, EmptyAndOneSidedBookMetrics) {
    EXPECT_EQ(ob->getMidPrice(), 0.0);
    EXPECT_EQ(ob->getSpread(), 0);
    EXPECT_EQ(ob->getImbalance(5), 0.0);
    EXPECT_EQ(ob->getBookPressure(5), 0.0);

    ob->addOrder(1, 100, 10, Side::BUY);

    EXPECT_EQ(ob->getMidPrice(), 0.0);
    EXPECT_EQ(ob->getSpread(), 0);

    EXPECT_DOUBLE_EQ(ob->getImbalance(5), 1.0);

    ob->clear();
    ob->addOrder(2, 100, 10, Side::SELL);

    EXPECT_DOUBLE_EQ(ob->getImbalance(5), -1.0);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, GetBidAskLevels) {
    EXPECT_EQ(ob->getBidLevels(), 0);
    EXPECT_EQ(ob->getAskLevels(), 0);

    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);
    ob->addOrder(3, 98, 30, Side::BUY);

    ob->addOrder(4, 101, 15, Side::SELL);
    ob->addOrder(5, 102, 25, Side::SELL);

    EXPECT_EQ(ob->getBidLevels(), 3);
    EXPECT_EQ(ob->getAskLevels(), 2);
}

TEST_F(OrderBookTest, GetPriceAtLevel) {
    // Bid: 100, 99, 98
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);
    ob->addOrder(3, 98, 30, Side::BUY);

    // Ask: 101, 102
    ob->addOrder(4, 101, 15, Side::SELL);
    ob->addOrder(5, 102, 25, Side::SELL);

    EXPECT_EQ(ob->getBidPrice(0), 100);  // Best bid
    EXPECT_EQ(ob->getBidPrice(1), 99);
    EXPECT_EQ(ob->getBidPrice(2), 98);

    EXPECT_EQ(ob->getAskPrice(0), 101);  // Best ask
    EXPECT_EQ(ob->getAskPrice(1), 102);
}

TEST_F(OrderBookTest, GetQtyAtLevel) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);
    ob->addOrder(3, 99, 30, Side::BUY);

    EXPECT_EQ(ob->getBidQty(0), 30);
    EXPECT_EQ(ob->getBidQty(1), 30);
}

TEST_F(OrderBookTest, GetLevelBeyondRange) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 101, 10, Side::SELL);

    EXPECT_EQ(ob->getBidPrice(5), 0);
    EXPECT_EQ(ob->getAskPrice(5), 0);
    EXPECT_EQ(ob->getBidQty(5), 0);
    EXPECT_EQ(ob->getAskQty(5), 0);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, GetOrderRank) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);
    ob->addOrder(3, 100, 30, Side::BUY);
    ob->addOrder(4, 99, 10, Side::BUY);

    EXPECT_EQ(ob->getOrderRank(1), 3);
    EXPECT_EQ(ob->getOrderRank(2), 3);
    EXPECT_EQ(ob->getOrderRank(3), 3);

    EXPECT_EQ(ob->getOrderRank(4), 1);
}

TEST_F(OrderBookTest, GetQtyAhead) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);
    ob->addOrder(3, 100, 30, Side::BUY);

    EXPECT_EQ(ob->getQtyAhead(3), 30);
    EXPECT_EQ(ob->getQtyAhead(2), 40);
    EXPECT_EQ(ob->getQtyAhead(1), 50);
}

TEST_F(OrderBookTest, OrderRankAfterDeletion) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);
    ob->addOrder(3, 100, 30, Side::BUY);

    ob->deleteOrder(2, Side::BUY);

    EXPECT_EQ(ob->getOrderRank(3), 2);
    EXPECT_EQ(ob->getQtyAhead(3), 10);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, WindowVolumeAndAmount) {
    uint64_t t = 1000000000ULL;

    ob->addOrder(1, 100, 100, Side::BUY);
    ob->addOrder(2, 110, 100, Side::SELL);

    ob->processTrade(1, 1, 100, 10, Side::BUY, t);

    EXPECT_EQ(ob->getWindowVolume(), 10);
    EXPECT_EQ(ob->getWindowAmount(), 1000);

    ob->processTrade(2, 2, 110, 20, Side::SELL, t + 1000);

    EXPECT_EQ(ob->getWindowVolume(), 30);
    EXPECT_EQ(ob->getWindowAmount(), 3200);  // 1000 + 2200
}

TEST_F(OrderBookTest, PartialFillAffectsVolume) {
    uint64_t t = 1000000000ULL;

    ob->addOrder(1, 100, 50, Side::SELL);
    ob->addOrder(2, 100, 100, Side::BUY);

    ob->processTrade(2, 1, 100, 30, Side::BUY, t);
    EXPECT_EQ(ob->getWindowVolume(), 30);
    EXPECT_EQ(ob->getWindowAmount(), 3000);

    ob->processTrade(2, 2, 100, 20, Side::BUY, t + 1000);
    EXPECT_EQ(ob->getWindowVolume(), 50);
    EXPECT_EQ(ob->getWindowAmount(), 5000);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, PriceRangeMultipleTrades) {
    uint64_t t = 1000000000ULL;

    ob->addOrder(1, 100, 100, Side::BUY);
    ob->addOrder(2, 105, 100, Side::BUY);
    ob->addOrder(3, 110, 100, Side::BUY);
    ob->addOrder(4, 95, 100, Side::BUY);

    ob->processTrade(1, 1, 100, 10, Side::BUY, t);
    ob->processTrade(2, 2, 105, 10, Side::BUY, t + 1000);
    ob->processTrade(3, 3, 110, 10, Side::BUY, t + 2000);
    ob->processTrade(4, 4, 95, 10, Side::BUY, t + 3000);

    EXPECT_EQ(ob->getPriceRange(), 15);
}

TEST_F(OrderBookTest, MedianPriceOddCount) {
    uint64_t t = 1000000000ULL;

    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 200, 10, Side::BUY);
    ob->addOrder(3, 150, 10, Side::BUY);
    ob->addOrder(4, 300, 10, Side::BUY);
    ob->addOrder(5, 250, 10, Side::BUY);

    ob->processTrade(1, 1, 100, 1, Side::BUY, t);
    ob->processTrade(2, 2, 200, 1, Side::BUY, t);
    ob->processTrade(3, 3, 150, 1, Side::BUY, t);
    ob->processTrade(4, 4, 300, 1, Side::BUY, t);
    ob->processTrade(5, 5, 250, 1, Side::BUY, t);

    EXPECT_EQ(ob->getMedianPrice(), 200);
}

TEST_F(OrderBookTest, MedianPriceEvenCount) {
    uint64_t t = 1000000000ULL;

    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 200, 10, Side::BUY);
    ob->addOrder(3, 150, 10, Side::BUY);
    ob->addOrder(4, 300, 10, Side::BUY);

    ob->processTrade(1, 1, 100, 1, Side::BUY, t);
    ob->processTrade(2, 2, 200, 1, Side::BUY, t);
    ob->processTrade(3, 3, 150, 1, Side::BUY, t);
    ob->processTrade(4, 4, 300, 1, Side::BUY, t);

    EXPECT_EQ(ob->getMedianPrice(), 175);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, PriceLevelDefinition) {
    // Bid: 100, 99, 98
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 10, Side::BUY);
    ob->addOrder(3, 98, 10, Side::BUY);

    // Ask: 101, 102, 103
    ob->addOrder(4, 101, 10, Side::SELL);
    ob->addOrder(5, 102, 10, Side::SELL);
    ob->addOrder(6, 103, 10, Side::SELL);

    uint64_t t = 1000000000ULL;
    ob->addOrder(100, 99, 1000, Side::BUY);
    ob->processTrade(100, 1, 99, 100, Side::BUY, t);

    EXPECT_EQ(ob->getVWAPLevel(), 1);
}

TEST_F(OrderBookTest, VWAPLevelAskSide) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 102, 10, Side::SELL);  // Level 0

    uint64_t t = 1000000000ULL;
    ob->addOrder(999, 103, 1000, Side::SELL);
    ob->processTrade(999, 1, 103, 100, Side::SELL, t);

    EXPECT_EQ(ob->getVWAPLevel(), -1);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, SamePriceOrdersFIFO) {
    for (int i = 0; i < 5; ++i) {
        ob->addOrder(i + 1, 100, 10 * (i + 1), Side::BUY);
    }

    EXPECT_EQ(ob->getBidQty(0), 150);

    EXPECT_EQ(ob->getOrderRank(5), 5);
    EXPECT_EQ(ob->getOrderRank(3), 5);

    EXPECT_EQ(ob->getQtyAhead(5), 100);
}

TEST_F(OrderBookTest, DeleteLastOrderAtPriceLevel) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);

    ob->deleteOrder(1, Side::BUY);

    EXPECT_EQ(ob->getBidPrice(0), 99);
    EXPECT_EQ(ob->getBidQty(0), 20);
}

TEST_F(OrderBookTest, MultipleLevelsSameSide) {
    for (int i = 0; i < 10; ++i) {
        ob->addOrder(i + 1, 100 - i, 10 * (i + 1), Side::BUY);
    }
    for (int i = 0; i < 10; ++i) {
        ob->addOrder(i + 100, 101 + i, 10 * (i + 1), Side::SELL);
    }

    EXPECT_EQ(ob->getBidLevels(), 10);
    EXPECT_EQ(ob->getAskLevels(), 10);

    EXPECT_EQ(ob->getBidPrice(0), 100);  // Best bid
    EXPECT_EQ(ob->getBidPrice(9), 91);   // Worst bid
    EXPECT_EQ(ob->getAskPrice(0), 101);  // Best ask
    EXPECT_EQ(ob->getAskPrice(9), 110);  // Worst ask
}

TEST_F(OrderBookTest, LargeOrderQuantity) {
    ob->addOrder(1, 100, UINT32_MAX, Side::BUY);
    ob->addOrder(2, 101, UINT32_MAX, Side::SELL);

    EXPECT_EQ(ob->getBidQty(0), UINT32_MAX);
    EXPECT_EQ(ob->getAskQty(0), UINT32_MAX);
}

TEST_F(OrderBookTest, NegativePrice) {
    ob->addOrder(1, -100, 10, Side::BUY);
    ob->addOrder(2, 100, 10, Side::SELL);

    EXPECT_EQ(ob->getBidPrice(0), -100);
    EXPECT_EQ(ob->getAskPrice(0), 100);
    EXPECT_DOUBLE_EQ(ob->getMidPrice(), 0.0);
}

TEST_F(OrderBookTest, ClearWithActiveOrders) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 101, 20, Side::SELL);

    uint64_t t = 1000000000ULL;
    ob->addOrder(3, 100, 100, Side::BUY);
    ob->processTrade(3, 1, 100, 50, Side::BUY, t);

    EXPECT_EQ(ob->getWindowVolume(), 50);

    ob->clear();

    EXPECT_EQ(ob->getBidLevels(), 0);
    EXPECT_EQ(ob->getAskLevels(), 0);
    EXPECT_EQ(ob->getWindowVolume(), 0);
    EXPECT_EQ(ob->getWindowAmount(), 0);
    EXPECT_EQ(ob->getMidPrice(), 0.0);
}

TEST_F(OrderBookTest, ZeroQuantityOrder) {
    ob->addOrder(1, 100, 0, Side::BUY);

    EXPECT_EQ(ob->getBidQty(0), 0);
}

TEST_F(OrderBookTest, ModifyToSamePrice) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);

    ob->modifyOrder(1, 100, 50, Side::BUY);

    EXPECT_EQ(ob->getBidQty(0), 70);
    EXPECT_EQ(ob->getBidPrice(0), 100);
}

TEST_F(OrderBookTest, ModifyPriceLevelChange) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);

    ob->modifyOrder(1, 95, 10, Side::BUY);

    EXPECT_EQ(ob->getBidPrice(0), 99);
    EXPECT_EQ(ob->getBidPrice(1), 95);
}

TEST_F(OrderBookTest, ImbalanceWithKLessThanLevels) {
    // Bid: 100(qty10), 99(qty10)
    // Ask: 101(qty10), 102(qty10)
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 10, Side::BUY);
    ob->addOrder(3, 98, 10, Side::BUY);

    ob->addOrder(4, 101, 10, Side::SELL);
    ob->addOrder(5, 102, 10, Side::SELL);
    ob->addOrder(6, 103, 10, Side::SELL);

    // Bid: 10+10=20, Ask: 10+10=20
    // Imbalance = (20-20)/(20+20) = 0
    EXPECT_DOUBLE_EQ(ob->getImbalance(2), 0.0);
}

TEST_F(OrderBookTest, ImbalanceWithKMoreThanLevels) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);
    ob->addOrder(3, 98, 30, Side::BUY);

    // Bid: 10+20+30=60, Ask: 0
    // Imbalance = (60-0)/(60+0) = 1.0
    EXPECT_DOUBLE_EQ(ob->getImbalance(10), 1.0);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, AddDeleteModifySequence) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 101, 10, Side::SELL);

    ob->deleteOrder(1, Side::BUY);
    EXPECT_EQ(ob->getBidLevels(), 0);

    ob->addOrder(3, 99, 20, Side::BUY);
    EXPECT_EQ(ob->getBidPrice(0), 99);

    ob->modifyOrder(3, 102, 30, Side::BUY);
    EXPECT_EQ(ob->getBidPrice(0), 102);
    EXPECT_EQ(ob->getBidQty(0), 30);
}

TEST_F(OrderBookTest, HighFrequencyTrades) {
    uint64_t t = 1000000000ULL;

    ob->addOrder(1, 100, 1000, Side::BUY);
    ob->addOrder(2, 101, 1000, Side::SELL);

    for (int i = 0; i < 100; ++i) {
        ob->processTrade(2, i, 101, 10, Side::SELL, t + i * 1000);
    }

    EXPECT_EQ(ob->getWindowVolume(), 1000);
    EXPECT_EQ(ob->getWindowAmount(), 101000);
    EXPECT_EQ(ob->getVWAP(), 101);
}

TEST_F(OrderBookTest, TradeEvictTradeCycle) {
    uint64_t base_ns = 1700000000000000000ULL;  // 1700000000 seconds

    ob->addOrder(1, 100, 100, Side::BUY);
    ob->processTrade(1, 1, 100, 10, Side::BUY, base_ns);

    EXPECT_EQ(ob->getWindowVolume(), 10);

    ob->addOrder(2, 110, 100, Side::BUY);
    ob->processTrade(2, 2, 110, 20, Side::BUY, base_ns + 601ULL * 1000000000ULL);

    ob->evictExpiredTrades(20231114222322ULL);

    EXPECT_EQ(ob->getWindowVolume(), 20);
    EXPECT_EQ(ob->getPriceRange(), 0);
}

// ==========================================
// ==========================================

TEST_F(OrderBookTest, SymmetricBook) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 10, Side::BUY);
    ob->addOrder(3, 98, 10, Side::BUY);

    ob->addOrder(4, 101, 10, Side::SELL);
    ob->addOrder(5, 102, 10, Side::SELL);
    ob->addOrder(6, 103, 10, Side::SELL);

    EXPECT_NEAR(ob->getImbalance(3), 0.0, 0.0001);

    // Mid price
    EXPECT_DOUBLE_EQ(ob->getMidPrice(), 100.5);
}

TEST_F(OrderBookTest, AsymmetricBook) {
    ob->addOrder(1, 100, 100, Side::BUY);  // BBO bid
    ob->addOrder(2, 99, 100, Side::BUY);
    ob->addOrder(3, 98, 100, Side::BUY);

    ob->addOrder(4, 101, 10, Side::SELL);  // BBO ask

    // Imbalance = (300-10)/(300+10) = 290/310 = 0.9355
    EXPECT_NEAR(ob->getImbalance(3), 0.9355, 0.001);

    // (ask_price * bid_qty + bid_price * ask_qty) / (bid_qty + ask_qty)
    // (101 * 100 + 100 * 10) / (100 + 10) = 100.909...
    EXPECT_NEAR(ob->getMacroPrice(), 100.909, 0.001);
}
