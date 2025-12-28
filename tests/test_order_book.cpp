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

TEST_F(OrderBookTest, ModifyOrderChangesBBO) {
    // 场景：当前 BBO 是 100 (qty 10)
    // 还有一个次优单 99 (qty 20)
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);

    EXPECT_EQ(ob->getBBO()->bid_price, 100);

    // 操作：将订单 1 的价格修改为 98 (变得比次优单还差)
    ob->modifyOrder(1, 98, 10, Side::BUY);

    // 验证：BBO 应该变成之前的次优单 (Price 99)
    EXPECT_EQ(ob->getBBO()->bid_price, 99);
    EXPECT_EQ(ob->getBBO()->bid_qty, 20);

    // 验证：订单 1 是否还在，且价格正确
    // 通过获取第2档（index 1）来验证
    EXPECT_EQ(ob->getBidPrice(1), 98);
}

TEST_F(OrderBookTest, TradePartialFillLogic) {
    uint64_t t = 1000000000ULL; // 1 sec
    
    // 卖单：价格 100，数量 50
    ob->addOrder(1, 100, 50, Side::SELL);
    
    // 发生一笔成交：消耗 20 个数量
    ob->processTrade(1, 1001, 100, 20, Side::SELL, t);

    // 验证：订单还在，数量剩下 30
    EXPECT_EQ(ob->getBBO()->ask_qty, 30);
    
    // 再次成交：消耗剩余的 30 个数量
    ob->processTrade(1, 1002, 100, 30, Side::SELL, t);

    // 验证：订单应该被移除，Ask 侧为空
    EXPECT_EQ(ob->getBBO()->ask_qty, 0);
    EXPECT_EQ(ob->getBBO()->ask_price, 0);
}

TEST_F(OrderBookTest, CalculateBookPressure) {
    // 构造一个对称但数量不对称的盘口
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
    
    // 注意：getMidPrice() = (99+101)/2 = 100.0
    
    EXPECT_NEAR(ob->getBookPressure(5), -0.2, 0.0001);
}

TEST_F(OrderBookTest, VWAPLevelMapping) {
    uint64_t t = 1000000000ULL;
    
    // 1. 构造盘口
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 10, Side::BUY);
    ob->addOrder(3, 98, 10, Side::BUY);

    ob->addOrder(4, 102, 10, Side::SELL); // Level 0
    ob->addOrder(5, 103, 10, Side::SELL); // Level 1

    // 2. 制造成交
    ob->addOrder(999, 99, 1000, Side::BUY); 
    ob->processTrade(999, 1, 99, 100, Side::BUY, t);
    
    // 此时 VWAP = 99
    EXPECT_EQ(ob->getVWAP(), 99);
    // 99 落在 Bid 的 Level 1 (价格99)
    EXPECT_EQ(ob->getVWAPLevel(), 1); 

    // 3. 拉高 VWAP
    // 修改：将价格改为 104，以确保均价能被拉高到 103
    // 计算: (99*100 + 104*10000) / 10100 = 103.95 -> 103
    ob->addOrder(888, 104, 20000, Side::SELL);  // <--- 改成 104
    ob->processTrade(888, 2, 104, 10000, Side::SELL, t); // <--- 改成 104
    
    // 现在的均价是 103
    EXPECT_EQ(ob->getVWAP(), 103);
    
    // 103 落在 Ask 的 Level 1 (Ask L0=102, Ask L1=103)
    // 根据逻辑 vwap_price <= price -> 103 <= 103 匹配 Level 1
    // 返回 -1
    EXPECT_EQ(ob->getVWAPLevel(), -1);
}


TEST_F(OrderBookTest, SlidingWindowEviction) {
    uint64_t t2_fmt = 20230101121001ULL; 
    uint64_t base_ns = 1672574400000000000ULL; 
    
    // T0 时刻
    // FIX: 先加单
    ob->addOrder(1, 100, 100, Side::BUY);
    ob->processTrade(1, 1, 100, 10, Side::BUY, base_ns);
    
    EXPECT_EQ(ob->getWindowVolume(), 10);
    EXPECT_EQ(ob->getPriceRange(), 0); 

    // T1 时刻 (+300s)
    // FIX: 先加单
    ob->addOrder(2, 110, 100, Side::BUY);
    ob->processTrade(2, 2, 110, 20, Side::BUY, base_ns + 300ULL * 1000000000ULL);
    
    EXPECT_EQ(ob->getWindowVolume(), 30); 
    EXPECT_EQ(ob->getPriceRange(), 10);   

    // T2 时刻过期
    ob->evictExpiredTrades(t2_fmt);

    EXPECT_EQ(ob->getWindowVolume(), 20); 
    EXPECT_EQ(ob->getPriceRange(), 0);    
    EXPECT_EQ(ob->getVWAP(), 110);
}

TEST_F(OrderBookTest, MedianPriceCalculation) {
    uint64_t t = 1000000000ULL;
    
    // FIX: 为每一笔成交先创建一个订单
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
    // 1. 空盘口
    EXPECT_EQ(ob->getMidPrice(), 0.0);
    EXPECT_EQ(ob->getSpread(), 0);
    EXPECT_EQ(ob->getImbalance(5), 0.0);
    EXPECT_EQ(ob->getBookPressure(5), 0.0);

    // 2. 只有买单
    ob->addOrder(1, 100, 10, Side::BUY);
    
    EXPECT_EQ(ob->getMidPrice(), 0.0); // 缺一边通常无法计算中价
    EXPECT_EQ(ob->getSpread(), 0);
    
    // Imbalance = (10 - 0) / 10 = 1.0 (全买方)
    EXPECT_DOUBLE_EQ(ob->getImbalance(5), 1.0);
    
    // 3. 只有卖单
    ob->clear();
    ob->addOrder(2, 100, 10, Side::SELL);
    
    // Imbalance = (0 - 10) / 10 = -1.0 (全卖方)
    EXPECT_DOUBLE_EQ(ob->getImbalance(5), -1.0);
}
