#include <gtest/gtest.h>
#include "impl/order_book.hpp"
#include "framework/define.hpp"

// 使用命名空间以便代码简洁
using namespace impl;

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook* ob;

    void SetUp() override {
        // 每个测试开始前创建新的 OrderBook
        ob = new OrderBook("TEST_SYMBOL");
    }

    void TearDown() override {
        // 每个测试结束后清理
        delete ob;
    }
};

// ==========================================
// 1. 测试订单逻辑 (Add/Modify/Delete)
// ==========================================

TEST_F(OrderBookTest, AddOrderUpdatesBBO) {
    // 初始状态为空
    EXPECT_EQ(ob->getBBO()->bid_price, 0);
    EXPECT_EQ(ob->getBBO()->ask_price, 0);

    // 添加一个买单：价格100，数量10
    ob->addOrder(1, 100, 10, Side::BUY);
    
    EXPECT_EQ(ob->getBBO()->bid_price, 100);
    EXPECT_EQ(ob->getBBO()->bid_qty, 10);

    // 添加一个更好的买单：价格101
    ob->addOrder(2, 101, 5, Side::BUY);

    EXPECT_EQ(ob->getBBO()->bid_price, 101); // BBO 应该更新
    EXPECT_EQ(ob->getBBO()->bid_qty, 5);
}

TEST_F(OrderBookTest, DeleteOrderUpdatesBBO) {
    ob->addOrder(1, 100, 10, Side::SELL);
    ob->addOrder(2, 102, 20, Side::SELL);

    // 当前最佳卖价是 100
    EXPECT_EQ(ob->getBBO()->ask_price, 100);

    // 删除最佳卖单
    ob->deleteOrder(1, Side::SELL);

    // BBO 应该回退到 102
    EXPECT_EQ(ob->getBBO()->ask_price, 102);
    EXPECT_EQ(ob->getBBO()->ask_qty, 20);
}

TEST_F(OrderBookTest, ModifyOrderLogic) {
    ob->addOrder(1, 100, 10, Side::BUY);
    
    // 修改订单数量 (不改价格)
    ob->modifyOrder(1, 100, 20, Side::BUY);
    EXPECT_EQ(ob->getBBO()->bid_qty, 20);

    // 修改订单价格 (这就涉及删除旧的，添加新的)
    ob->modifyOrder(1, 105, 20, Side::BUY);
    EXPECT_EQ(ob->getBBO()->bid_price, 105);
}

// ==========================================
// 2. 测试信号计算 (Signals)
// ==========================================

TEST_F(OrderBookTest, SignalMidPriceAndSpread) {
    // 构建一个典型的盘口
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
    ob->addOrder(2, 99, 20, Side::BUY); // 深度为2
    ob->addOrder(3, 110, 10, Side::SELL);

    // Imbalance Formula: (BidQty - AskQty) / (BidQty + AskQty)
    // K=5 (计算前5层)
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
    // 价格偏向量大的一边(Ask)，说明买方力量弱，价格偏向100
    // 修正: 代码逻辑是 (Ask * BidWt + Bid * AskWt) 
    // Wait, let's check code logic:
    // return (bbo_.ask_price * bid_weight + bbo_.bid_price * ask_weight) ...
    // Macro Price 应该靠近量大的一方？通常微观价格会向量大的一方移动？
    // 或者这是定义问题。按照代码逻辑：
    // (110 * 10 + 100 * 30) / 40 = 102.5
    
    EXPECT_DOUBLE_EQ(ob->getMacroPrice(), 102.5);
}

// ==========================================
// 3. 测试成交与时间窗口 (VWAP/Volume)
// ==========================================

TEST_F(OrderBookTest, TradeProcessingAndVWAP) {
    // 模拟成交
    // 时间戳需要很大 (代码中是 nanoseconds)
    // 假设 2023-01-01 12:00:00 左右
    uint64_t time1 = 1672574400000000000ULL; 
    
    // OrderBook 需要先有订单才能撮合（逻辑上），但在 processTrade 中
    // 你是直接更新 window_stats 的，即使没有对应的 order_id 也能测试统计逻辑
    // 但为了严谨，我们先加个单
    ob->addOrder(10, 100, 100, Side::BUY);
    
    // 发生成交: Price 100, Qty 10
    ob->processTrade(10, 999, 100, 10, Side::BUY, time1);

    // 此时 Window Volume = 10
    EXPECT_EQ(ob->getWindowVolume(), 10);
    // VWAP = (100 * 10) / 10 = 100
    EXPECT_EQ(ob->getVWAP(), 100);

    // 第二笔成交: Price 110, Qty 10
    ob->addOrder(11, 110, 100, Side::SELL);
    ob->processTrade(11, 1000, 110, 10, Side::SELL, time1 + 1000); // +1us

    // Total Volume = 20
    EXPECT_EQ(ob->getWindowVolume(), 20);
    // Total Amount = 100*10 + 110*10 = 1000 + 1100 = 2100
    // VWAP = 2100 / 20 = 105
    EXPECT_EQ(ob->getVWAP(), 105);
}
