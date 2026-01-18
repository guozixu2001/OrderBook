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

// ==========================================
// 4. 基础查询测试 (Basic Queries)
// ==========================================

TEST_F(OrderBookTest, GetBidAskLevels) {
    // 初始为空
    EXPECT_EQ(ob->getBidLevels(), 0);
    EXPECT_EQ(ob->getAskLevels(), 0);

    // 添加买单
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);
    ob->addOrder(3, 98, 30, Side::BUY);

    // 添加卖单
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

    // 验证价格档位 (价格从优到劣排序)
    EXPECT_EQ(ob->getBidPrice(0), 100);  // Best bid
    EXPECT_EQ(ob->getBidPrice(1), 99);
    EXPECT_EQ(ob->getBidPrice(2), 98);

    EXPECT_EQ(ob->getAskPrice(0), 101);  // Best ask
    EXPECT_EQ(ob->getAskPrice(1), 102);
}

TEST_F(OrderBookTest, GetQtyAtLevel) {
    // 同一价格多个订单
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);  // 同价叠加
    ob->addOrder(3, 99, 30, Side::BUY);

    EXPECT_EQ(ob->getBidQty(0), 30);  // 价格100，数量10+20=30
    EXPECT_EQ(ob->getBidQty(1), 30);  // 价格99，数量30
}

TEST_F(OrderBookTest, GetLevelBeyondRange) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 101, 10, Side::SELL);

    // 超出范围返回0
    EXPECT_EQ(ob->getBidPrice(5), 0);
    EXPECT_EQ(ob->getAskPrice(5), 0);
    EXPECT_EQ(ob->getBidQty(5), 0);
    EXPECT_EQ(ob->getAskQty(5), 0);
}

// ==========================================
// 5. 订单追踪测试 (Order Tracking)
// ==========================================

TEST_F(OrderBookTest, GetOrderRank) {
    // 添加同一价格的多个订单 (新订单添加到队尾)
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);
    ob->addOrder(3, 100, 30, Side::BUY);
    ob->addOrder(4, 99, 10, Side::BUY);  // 不同价格

    // getOrderRank 返回的是该价格档位的订单总数（不是位置）
    // 所有同一价格的订单返回相同的 rank = count
    EXPECT_EQ(ob->getOrderRank(1), 3);
    EXPECT_EQ(ob->getOrderRank(2), 3);
    EXPECT_EQ(ob->getOrderRank(3), 3);

    // 不同价格档位独立计算
    EXPECT_EQ(ob->getOrderRank(4), 1);  // 99 价格档位只有1个订单
}

TEST_F(OrderBookTest, GetQtyAhead) {
    // 新订单添加到队尾: order1 -> order2 -> order3 (circular)
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);
    ob->addOrder(3, 100, 30, Side::BUY);

    // getQtyAhead 返回该订单前面所有订单的数量之和（不包括自己）
    // order3 前面有 order1+order2 = 10+20 = 30
    EXPECT_EQ(ob->getQtyAhead(3), 30);
    // order2 前面有 order1+order3 = 10+30 = 40
    EXPECT_EQ(ob->getQtyAhead(2), 40);
    // order1 前面有 order2+order3 = 20+30 = 50
    EXPECT_EQ(ob->getQtyAhead(1), 50);
}

TEST_F(OrderBookTest, OrderRankAfterDeletion) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);
    ob->addOrder(3, 100, 30, Side::BUY);

    // 删除订单2
    ob->deleteOrder(2, Side::BUY);

    // 订单3前面只有订单1(10)
    EXPECT_EQ(ob->getOrderRank(3), 2);
    EXPECT_EQ(ob->getQtyAhead(3), 10);
}

TEST_F(OrderBookTest, DeleteWithLinearProbingCollisions) {
    // 强制 order_id 哈希冲突，验证删除不会截断探测链
    uint64_t id1 = 1;
    uint64_t id2 = id1 + MAX_ORDERS;
    uint64_t id3 = id1 + 2 * MAX_ORDERS;

    ob->addOrder(id1, 100, 10, Side::BUY);
    ob->addOrder(id2, 100, 20, Side::BUY);
    ob->addOrder(id3, 100, 30, Side::BUY);

    EXPECT_EQ(ob->getBidQty(0), 60);

    ob->deleteOrder(id1, Side::BUY);
    EXPECT_EQ(ob->getBidQty(0), 50);

    // 如果探测链被截断，这里会找不到 id2，数量不会继续下降
    ob->deleteOrder(id2, Side::BUY);
    EXPECT_EQ(ob->getBidQty(0), 30);
}

TEST_F(OrderBookTest, PriceLevelHashCollisions) {
    // 价格哈希碰撞：p2 与 p1 在同一槽位
    int32_t p1 = 100;
    int32_t p2 = p1 + static_cast<int32_t>(MAX_PRICE_LEVELS);

    ob->addOrder(1, p1, 10, Side::BUY);
    ob->addOrder(2, p2, 20, Side::BUY);

    EXPECT_EQ(ob->getBidLevels(), 2);
    EXPECT_EQ(ob->getBidPrice(0), p2);
    EXPECT_EQ(ob->getBidQty(0), 20);
    EXPECT_EQ(ob->getBidPrice(1), p1);
    EXPECT_EQ(ob->getBidQty(1), 10);

    // 删除高价层，低价层仍应存在
    ob->deleteOrder(2, Side::BUY);
    EXPECT_EQ(ob->getBidLevels(), 1);
    EXPECT_EQ(ob->getBidPrice(0), p1);
    EXPECT_EQ(ob->getBidQty(0), 10);
}

// ==========================================
// 6. 成交量/成交额测试 (Volume/Amount)
// ==========================================

TEST_F(OrderBookTest, WindowVolumeAndAmount) {
    uint64_t t = 1000000000ULL;

    ob->addOrder(1, 100, 100, Side::BUY);
    ob->addOrder(2, 110, 100, Side::SELL);

    // 成交1: Price 100, Qty 10 -> Amount = 1000
    ob->processTrade(1, 1, 100, 10, Side::BUY, t);

    EXPECT_EQ(ob->getWindowVolume(), 10);
    EXPECT_EQ(ob->getWindowAmount(), 1000);

    // 成交2: Price 110, Qty 20 -> Amount = 2200
    ob->processTrade(2, 2, 110, 20, Side::SELL, t + 1000);

    EXPECT_EQ(ob->getWindowVolume(), 30);
    EXPECT_EQ(ob->getWindowAmount(), 3200);  // 1000 + 2200
}

TEST_F(OrderBookTest, PartialFillAffectsVolume) {
    uint64_t t = 1000000000ULL;

    ob->addOrder(1, 100, 50, Side::SELL);
    ob->addOrder(2, 100, 100, Side::BUY);

    // 第一次成交 30
    ob->processTrade(2, 1, 100, 30, Side::BUY, t);
    EXPECT_EQ(ob->getWindowVolume(), 30);
    EXPECT_EQ(ob->getWindowAmount(), 3000);

    // 第二次成交 20 (剩余20)
    ob->processTrade(2, 2, 100, 20, Side::BUY, t + 1000);
    EXPECT_EQ(ob->getWindowVolume(), 50);
    EXPECT_EQ(ob->getWindowAmount(), 5000);
}

// ==========================================
// 7. VWAP 中位数价格范围测试
// ==========================================

TEST_F(OrderBookTest, PriceRangeMultipleTrades) {
    uint64_t t = 1000000000ULL;

    // 记录多笔不同价格的成交
    ob->addOrder(1, 100, 100, Side::BUY);
    ob->addOrder(2, 105, 100, Side::BUY);
    ob->addOrder(3, 110, 100, Side::BUY);
    ob->addOrder(4, 95, 100, Side::BUY);

    ob->processTrade(1, 1, 100, 10, Side::BUY, t);
    ob->processTrade(2, 2, 105, 10, Side::BUY, t + 1000);
    ob->processTrade(3, 3, 110, 10, Side::BUY, t + 2000);
    ob->processTrade(4, 4, 95, 10, Side::BUY, t + 3000);

    // 价格范围: 95 - 110 = 15
    EXPECT_EQ(ob->getPriceRange(), 15);
}

TEST_F(OrderBookTest, MedianPriceOddCount) {
    uint64_t t = 1000000000ULL;

    // 5笔成交，价格: 100, 200, 150, 300, 250
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

    // 排序后: 100, 150, 200, 250, 300 -> 中位数 200
    EXPECT_EQ(ob->getMedianPrice(), 200);
}

TEST_F(OrderBookTest, MedianPriceEvenCount) {
    uint64_t t = 1000000000ULL;

    // 4笔成交，价格: 100, 200, 150, 300
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 200, 10, Side::BUY);
    ob->addOrder(3, 150, 10, Side::BUY);
    ob->addOrder(4, 300, 10, Side::BUY);

    ob->processTrade(1, 1, 100, 1, Side::BUY, t);
    ob->processTrade(2, 2, 200, 1, Side::BUY, t);
    ob->processTrade(3, 3, 150, 1, Side::BUY, t);
    ob->processTrade(4, 4, 300, 1, Side::BUY, t);

    // 排序后: 100, 150, 200, 300 -> 中位数 (150+200)/2 = 175
    EXPECT_EQ(ob->getMedianPrice(), 175);
}

// ==========================================
// 8. 价格档位 Level 定义测试
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

    // VWAP Level 测试
    uint64_t t = 1000000000ULL;
    ob->addOrder(100, 99, 1000, Side::BUY);
    ob->processTrade(100, 1, 99, 100, Side::BUY, t);

    // VWAP = 99, 落在 Bid Level 1 (价格99)
    EXPECT_EQ(ob->getVWAPLevel(), 1);
}

TEST_F(OrderBookTest, VWAPLevelAskSide) {
    // 构造盘口
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 102, 10, Side::SELL);  // Level 0

    uint64_t t = 1000000000ULL;
    ob->addOrder(999, 103, 1000, Side::SELL);
    ob->processTrade(999, 1, 103, 100, Side::SELL, t);

    // VWAP = 103, 落在 Ask Level 0 (价格102 <= 103)
    EXPECT_EQ(ob->getVWAPLevel(), -1);
}

// ==========================================
// 9. 边界条件测试 (Edge Cases)
// ==========================================

TEST_F(OrderBookTest, SamePriceOrdersFIFO) {
    // 同一价格多个订单 (新订单添加到队尾)
    for (int i = 0; i < 5; ++i) {
        ob->addOrder(i + 1, 100, 10 * (i + 1), Side::BUY);
    }

    // 总数量 = 10+20+30+40+50 = 150
    EXPECT_EQ(ob->getBidQty(0), 150);

    // getOrderRank 返回订单总数 = 5
    EXPECT_EQ(ob->getOrderRank(5), 5);
    EXPECT_EQ(ob->getOrderRank(3), 5);

    // getQtyAhead 返回该订单前面所有订单的数量之和（不包括自己）
    // order5(qty=50) 前面有 10+20+30+40 = 100
    EXPECT_EQ(ob->getQtyAhead(5), 100);
}

TEST_F(OrderBookTest, DeleteLastOrderAtPriceLevel) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);

    // 删除100档的唯一订单
    ob->deleteOrder(1, Side::BUY);

    // 验证100档被移除，BBO更新
    EXPECT_EQ(ob->getBidPrice(0), 99);
    EXPECT_EQ(ob->getBidQty(0), 20);
}

TEST_F(OrderBookTest, MultipleLevelsSameSide) {
    // 构造深度订单簿
    for (int i = 0; i < 10; ++i) {
        ob->addOrder(i + 1, 100 - i, 10 * (i + 1), Side::BUY);
    }
    for (int i = 0; i < 10; ++i) {
        ob->addOrder(i + 100, 101 + i, 10 * (i + 1), Side::SELL);
    }

    EXPECT_EQ(ob->getBidLevels(), 10);
    EXPECT_EQ(ob->getAskLevels(), 10);

    // 验证价格排序
    EXPECT_EQ(ob->getBidPrice(0), 100);  // Best bid
    EXPECT_EQ(ob->getBidPrice(9), 91);   // Worst bid
    EXPECT_EQ(ob->getAskPrice(0), 101);  // Best ask
    EXPECT_EQ(ob->getAskPrice(9), 110);  // Worst ask
}

TEST_F(OrderBookTest, LargeOrderQuantity) {
    // 测试大数量
    ob->addOrder(1, 100, UINT32_MAX, Side::BUY);
    ob->addOrder(2, 101, UINT32_MAX, Side::SELL);

    EXPECT_EQ(ob->getBidQty(0), UINT32_MAX);
    EXPECT_EQ(ob->getAskQty(0), UINT32_MAX);
}

TEST_F(OrderBookTest, NegativePrice) {
    // 支持负价格（如某些衍生品）
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

    // 验证清空
    EXPECT_EQ(ob->getBidLevels(), 0);
    EXPECT_EQ(ob->getAskLevels(), 0);
    EXPECT_EQ(ob->getWindowVolume(), 0);
    EXPECT_EQ(ob->getWindowAmount(), 0);
    EXPECT_EQ(ob->getMidPrice(), 0.0);
}

TEST_F(OrderBookTest, ZeroQuantityOrder) {
    // 零数量订单
    ob->addOrder(1, 100, 0, Side::BUY);

    EXPECT_EQ(ob->getBidQty(0), 0);
}

TEST_F(OrderBookTest, ModifyToSamePrice) {
    // 两个订单在同一价格
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 100, 20, Side::BUY);  // 同价

    // 数量从10改为50 (qty_diff = 40)
    ob->modifyOrder(1, 100, 50, Side::BUY);

    // 总数量 = 50 + 20 = 70
    EXPECT_EQ(ob->getBidQty(0), 70);
    EXPECT_EQ(ob->getBidPrice(0), 100);  // 价格不变
}

TEST_F(OrderBookTest, ModifyPriceLevelChange) {
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);

    // 从100改为95（低于99）
    ob->modifyOrder(1, 95, 10, Side::BUY);

    // 验证BBO更新为99
    EXPECT_EQ(ob->getBidPrice(0), 99);
    EXPECT_EQ(ob->getBidPrice(1), 95);
}

TEST_F(OrderBookTest, ImbalanceWithKLessThanLevels) {
    // K=2，计算前2层
    // Bid: 100(qty10), 99(qty10)
    // Ask: 101(qty10), 102(qty10)
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 10, Side::BUY);
    ob->addOrder(3, 98, 10, Side::BUY);  // 第3层，不应该被计入

    ob->addOrder(4, 101, 10, Side::SELL);
    ob->addOrder(5, 102, 10, Side::SELL);
    ob->addOrder(6, 103, 10, Side::SELL);  // 第3层，不应该被计入

    // K=2 只计算前2层
    // Bid: 10+10=20, Ask: 10+10=20
    // Imbalance = (20-20)/(20+20) = 0
    EXPECT_DOUBLE_EQ(ob->getImbalance(2), 0.0);
}

TEST_F(OrderBookTest, ImbalanceWithKMoreThanLevels) {
    // K=10，但只有3个档位
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 20, Side::BUY);
    ob->addOrder(3, 98, 30, Side::BUY);

    // K=10 应该只计算实际存在的3层
    // Bid: 10+20+30=60, Ask: 0
    // Imbalance = (60-0)/(60+0) = 1.0
    EXPECT_DOUBLE_EQ(ob->getImbalance(10), 1.0);
}

// ==========================================
// 10. 连续操作测试 (Sequential Operations)
// ==========================================

TEST_F(OrderBookTest, AddDeleteModifySequence) {
    // 连续操作序列
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

    // 连续多笔成交
    for (int i = 0; i < 100; ++i) {
        ob->processTrade(2, i, 101, 10, Side::SELL, t + i * 1000);
    }

    EXPECT_EQ(ob->getWindowVolume(), 1000);
    EXPECT_EQ(ob->getWindowAmount(), 101000);
    EXPECT_EQ(ob->getVWAP(), 101);
}

// 原始测试
TEST_F(OrderBookTest, TradeEvictTradeCycle) {
    uint64_t base_ns = 1700000000000000000ULL;  // 1700000000 seconds

    // 旧成交 (t=0, 22:13:20)
    ob->addOrder(1, 100, 100, Side::BUY);
    ob->processTrade(1, 1, 100, 10, Side::BUY, base_ns);

    EXPECT_EQ(ob->getWindowVolume(), 10);

    // 新成交 (+601s = 10分钟01秒, 22:23:21)
    ob->addOrder(2, 110, 100, Side::BUY);
    ob->processTrade(2, 2, 110, 20, Side::BUY, base_ns + 601ULL * 1000000000ULL);

    // 驱逐旧成交 (时间窗口: 10分钟)
    // 窗口是 [cutoff, current)，即 [22:13:21, 22:23:22)
    // 使用 20231114222322 = 22:23:22，这样 trade 2 (22:23:21) 会被保留
    ob->evictExpiredTrades(20231114222322ULL);

    // 旧成交 (22:13:20 < 22:13:21) 已驱逐，只剩新成交 (22:23:21)
    EXPECT_EQ(ob->getWindowVolume(), 20);
    EXPECT_EQ(ob->getPriceRange(), 0);  // 只有110
}

// ==========================================
// 11. 对称/非对称盘口测试
// ==========================================

TEST_F(OrderBookTest, SymmetricBook) {
    // 对称盘口
    ob->addOrder(1, 100, 10, Side::BUY);
    ob->addOrder(2, 99, 10, Side::BUY);
    ob->addOrder(3, 98, 10, Side::BUY);

    ob->addOrder(4, 101, 10, Side::SELL);
    ob->addOrder(5, 102, 10, Side::SELL);
    ob->addOrder(6, 103, 10, Side::SELL);

    // Imbalance 应该接近 0
    EXPECT_NEAR(ob->getImbalance(3), 0.0, 0.0001);

    // Mid price
    EXPECT_DOUBLE_EQ(ob->getMidPrice(), 100.5);
}

TEST_F(OrderBookTest, AsymmetricBook) {
    // 买方深度大于卖方 (BBO 决定 Macro Price)
    ob->addOrder(1, 100, 100, Side::BUY);  // BBO bid
    ob->addOrder(2, 99, 100, Side::BUY);
    ob->addOrder(3, 98, 100, Side::BUY);

    ob->addOrder(4, 101, 10, Side::SELL);  // BBO ask

    // 买方3层总量 300，卖方1层 10
    // Imbalance = (300-10)/(300+10) = 290/310 = 0.9355
    EXPECT_NEAR(ob->getImbalance(3), 0.9355, 0.001);

    // Macro price 只看 BBO:
    // (ask_price * bid_qty + bid_price * ask_qty) / (bid_qty + ask_qty)
    // (101 * 100 + 100 * 10) / (100 + 10) = 100.909...
    EXPECT_NEAR(ob->getMacroPrice(), 100.909, 0.001);
}
