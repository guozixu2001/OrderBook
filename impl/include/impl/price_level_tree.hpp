#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#ifndef PRICE_LEVEL_TREE_LEAF_MAX
// Tuned for ~512B leaf nodes (fits well in L1 with 64B lines).
#define PRICE_LEVEL_TREE_LEAF_MAX 40
#endif

#ifndef PRICE_LEVEL_TREE_INTERNAL_MAX
// Match leaf fanout for balanced depth and cache behavior.
#define PRICE_LEVEL_TREE_INTERNAL_MAX 40
#endif

namespace impl {

class PriceLevelTree {
public:
  using LevelId = size_t;
  static constexpr LevelId kInvalidLevel = std::numeric_limits<LevelId>::max();

  PriceLevelTree() = default;
  ~PriceLevelTree() = default;

  void clear() {
    root_ = nullptr;
    leftmost_ = nullptr;
    rightmost_ = nullptr;
    size_ = 0;
    inserts_since_rebuild_ = 0;
    erases_since_rebuild_ = 0;
    leaf_pool_.clear();
    internal_pool_.clear();
  }

  size_t size() const { return size_; }

  void setAutoRebuild(bool enabled) { auto_rebuild_ = enabled; }

  void rebuild() {
    if (size_ == 0) {
      clear();
      return;
    }

    std::vector<int32_t> keys;
    std::vector<LevelId> values;
    keys.reserve(size_);
    values.reserve(size_);

    const LeafNode* leaf = leftmostNonEmpty();
    while (leaf) {
      for (uint16_t i = 0; i < leaf->count; ++i) {
        keys.push_back(leaf->keys[i]);
        values.push_back(leaf->values[i]);
      }
      leaf = nextNonEmpty(leaf->next);
    }

    clear();
    buildFromSorted(keys, values);
  }

  bool find(int32_t price, LevelId* out_level) const {
    if (!root_) return false;
    const LeafNode* leaf = findLeaf(price);
    if (!leaf || leaf->count == 0) return false;
    const int32_t* keys = leaf->keys;
    const uint16_t count = leaf->count;
    const int32_t* it = std::lower_bound(keys, keys + count, price);
    if (it == keys + count || *it != price) return false;
    size_t idx = static_cast<size_t>(it - keys);
    if (out_level) {
      *out_level = leaf->values[idx];
    }
    return true;
  }

  bool insert(int32_t price, LevelId level) {
    if (!root_) {
      LeafNode* leaf = leaf_pool_.allocate();
      if (!leaf) return false;
      leaf->keys[0] = price;
      leaf->values[0] = level;
      leaf->count = 1;
      root_ = leaf;
      leftmost_ = leaf;
      rightmost_ = leaf;
      size_ = 1;
      inserts_since_rebuild_++;
      return true;
    }

    LeafNode* leaf = findLeaf(price);
    if (!leaf) return false;
    int pos = lowerBound(leaf, price);
    if (pos < leaf->count && leaf->keys[pos] == price) {
      return false;
    }

    insertIntoLeaf(leaf, pos, price, level);
    ++size_;
    inserts_since_rebuild_++;

    LeafNode* target_leaf = leaf;
    int inserted_pos = pos;

    if (leaf->count > kLeafMax) {
      LeafNode* new_leaf = splitLeaf(leaf);
      if (!new_leaf) return true;
      if (price >= new_leaf->keys[0]) {
        target_leaf = new_leaf;
        inserted_pos = lowerBound(new_leaf, price);
      }
    }

    if (inserted_pos == 0) {
      updateMinKeyAfterInsert(target_leaf, target_leaf->keys[0]);
    }

    return true;
  }

  bool erase(int32_t price) {
    if (!root_) return false;
    LeafNode* leaf = findLeaf(price);
    if (!leaf || leaf->count == 0) return false;
    int pos = lowerBound(leaf, price);
    if (pos >= leaf->count || leaf->keys[pos] != price) {
      return false;
    }

    removeFromLeaf(leaf, pos);
    if (size_ > 0) {
      --size_;
    }
    erases_since_rebuild_++;
    maybeRebuild();
    return true;
  }

  bool min(int32_t* price, LevelId* level) const {
    const LeafNode* leaf = leftmostNonEmpty();
    if (!leaf) return false;
    if (price) *price = leaf->keys[0];
    if (level) *level = leaf->values[0];
    return true;
  }

  bool max(int32_t* price, LevelId* level) const {
    const LeafNode* leaf = rightmostNonEmpty();
    if (!leaf) return false;
    if (price) *price = leaf->keys[leaf->count - 1];
    if (level) *level = leaf->values[leaf->count - 1];
    return true;
  }

  bool nthFromMin(size_t n, int32_t* price, LevelId* level) const {
    const LeafNode* leaf = leftmostNonEmpty();
    while (leaf) {
      if (n < leaf->count) {
        if (price) *price = leaf->keys[n];
        if (level) *level = leaf->values[n];
        return true;
      }
      n -= leaf->count;
      leaf = nextNonEmpty(leaf->next);
    }
    return false;
  }

  bool nthFromMax(size_t n, int32_t* price, LevelId* level) const {
    const LeafNode* leaf = rightmostNonEmpty();
    while (leaf) {
      if (n < leaf->count) {
        size_t idx = static_cast<size_t>(leaf->count - 1) - n;
        if (price) *price = leaf->keys[idx];
        if (level) *level = leaf->values[idx];
        return true;
      }
      n -= leaf->count;
      leaf = prevNonEmpty(leaf->prev);
    }
    return false;
  }

  template <typename Fn>
  void forEachFromMin(size_t k, Fn&& fn) const {
    const LeafNode* leaf = leftmostNonEmpty();
    size_t remaining = k;
    while (leaf && remaining > 0) {
      for (uint16_t i = 0; i < leaf->count && remaining > 0; ++i) {
        fn(leaf->keys[i], leaf->values[i]);
        --remaining;
      }
      leaf = nextNonEmpty(leaf->next);
    }
  }

  template <typename Fn>
  void forEachFromMax(size_t k, Fn&& fn) const {
    const LeafNode* leaf = rightmostNonEmpty();
    size_t remaining = k;
    while (leaf && remaining > 0) {
      for (int i = static_cast<int>(leaf->count) - 1; i >= 0 && remaining > 0; --i) {
        fn(leaf->keys[i], leaf->values[i]);
        --remaining;
      }
      leaf = prevNonEmpty(leaf->prev);
    }
  }

private:
  static constexpr uint16_t kLeafMax = PRICE_LEVEL_TREE_LEAF_MAX;
  static constexpr uint16_t kInternalMax = PRICE_LEVEL_TREE_INTERNAL_MAX;

  struct InternalNode;

  struct NodeBase {
    bool leaf = false;
    uint16_t count = 0;
    InternalNode* parent = nullptr;
  };

  struct LeafNode : NodeBase {
    int32_t keys[kLeafMax + 1];
    LevelId values[kLeafMax + 1];
    LeafNode* next = nullptr;
    LeafNode* prev = nullptr;

    void reset() {
      leaf = true;
      count = 0;
      parent = nullptr;
      next = nullptr;
      prev = nullptr;
    }
  };

  struct InternalNode : NodeBase {
    int32_t keys[kInternalMax + 1];
    NodeBase* children[kInternalMax + 2];

    void reset() {
      leaf = false;
      count = 0;
      parent = nullptr;
    }
  };

  template <typename T, size_t ChunkSize = 256>
  class NodePool {
  public:
    T* allocate() {
      if (!free_list_.empty()) {
        T* node = free_list_.back();
        free_list_.pop_back();
        node->reset();
        return node;
      }
      if (chunks_.empty() || chunks_.back().used == ChunkSize) {
        Chunk chunk;
        chunk.data = std::unique_ptr<T[]>(new (std::nothrow) T[ChunkSize]);
        if (!chunk.data) return nullptr;
        chunk.used = 0;
        chunks_.push_back(std::move(chunk));
      }
      T* node = &chunks_.back().data[chunks_.back().used++];
      ++allocated_;
      node->reset();
      return node;
    }

    void clear() {
      chunks_.clear();
      free_list_.clear();
      allocated_ = 0;
    }

    size_t allocatedCount() const { return allocated_; }

  private:
    struct Chunk {
      std::unique_ptr<T[]> data;
      size_t used = 0;
    };

    std::vector<Chunk> chunks_;
    std::vector<T*> free_list_;
    size_t allocated_ = 0;
  };

  LeafNode* findLeaf(int32_t price) const {
    NodeBase* node = root_;
    while (node && !node->leaf) {
      InternalNode* internal = static_cast<InternalNode*>(node);
      size_t idx = upperBound(internal, price);
      node = internal->children[idx];
    }
    return static_cast<LeafNode*>(node);
  }

  static size_t upperBound(const InternalNode* node, int32_t price) {
    const int32_t* keys = node->keys;
    return static_cast<size_t>(std::upper_bound(keys, keys + node->count, price) - keys);
  }

  static int lowerBound(const LeafNode* node, int32_t price) {
    const int32_t* keys = node->keys;
    return static_cast<int>(std::lower_bound(keys, keys + node->count, price) - keys);
  }

  void insertIntoLeaf(LeafNode* leaf, int pos, int32_t price, LevelId level) {
    if (pos < leaf->count) {
      std::memmove(&leaf->keys[pos + 1], &leaf->keys[pos],
                   (leaf->count - pos) * sizeof(int32_t));
      std::memmove(&leaf->values[pos + 1], &leaf->values[pos],
                   (leaf->count - pos) * sizeof(LevelId));
    }
    leaf->keys[pos] = price;
    leaf->values[pos] = level;
    ++leaf->count;
  }

  void removeFromLeaf(LeafNode* leaf, int pos) {
    if (pos < leaf->count - 1) {
      std::memmove(&leaf->keys[pos], &leaf->keys[pos + 1],
                   (leaf->count - pos - 1) * sizeof(int32_t));
      std::memmove(&leaf->values[pos], &leaf->values[pos + 1],
                   (leaf->count - pos - 1) * sizeof(LevelId));
    }
    --leaf->count;
  }

  LeafNode* splitLeaf(LeafNode* leaf) {
    LeafNode* new_leaf = leaf_pool_.allocate();
    if (!new_leaf) return nullptr;

    const uint16_t total = leaf->count;
    const uint16_t split = static_cast<uint16_t>(total / 2);
    const uint16_t right_count = static_cast<uint16_t>(total - split);

    std::memcpy(new_leaf->keys, leaf->keys + split, right_count * sizeof(int32_t));
    std::memcpy(new_leaf->values, leaf->values + split, right_count * sizeof(LevelId));
    new_leaf->count = right_count;

    leaf->count = split;

    new_leaf->next = leaf->next;
    new_leaf->prev = leaf;
    if (leaf->next) {
      leaf->next->prev = new_leaf;
    }
    leaf->next = new_leaf;

    if (rightmost_ == leaf) {
      rightmost_ = new_leaf;
    }

    new_leaf->parent = leaf->parent;

    insertIntoParent(leaf, new_leaf->keys[0], new_leaf);
    return new_leaf;
  }

  void insertIntoParent(NodeBase* left, int32_t key, NodeBase* right) {
    InternalNode* parent = left->parent;
    if (!parent) {
      InternalNode* new_root = internal_pool_.allocate();
      if (!new_root) return;
      new_root->keys[0] = key;
      new_root->children[0] = left;
      new_root->children[1] = right;
      new_root->count = 1;
      left->parent = new_root;
      right->parent = new_root;
      root_ = new_root;
      return;
    }

    size_t insert_pos = findChildIndex(parent, left);
    if (insert_pos > parent->count) {
      insert_pos = parent->count;
    }

    if (insert_pos < parent->count) {
      std::memmove(&parent->keys[insert_pos + 1], &parent->keys[insert_pos],
                   (parent->count - insert_pos) * sizeof(int32_t));
      std::memmove(&parent->children[insert_pos + 2], &parent->children[insert_pos + 1],
                   (parent->count - insert_pos + 1) * sizeof(NodeBase*));
    }

    parent->keys[insert_pos] = key;
    parent->children[insert_pos + 1] = right;
    ++parent->count;
    right->parent = parent;

    if (parent->count > kInternalMax) {
      splitInternal(parent);
    }
  }

  void splitInternal(InternalNode* node) {
    const uint16_t total = node->count;
    const uint16_t mid = static_cast<uint16_t>(total / 2);
    const int32_t promote_key = node->keys[mid];

    InternalNode* new_node = internal_pool_.allocate();
    if (!new_node) return;

    const uint16_t right_count = static_cast<uint16_t>(total - mid - 1);
    std::memcpy(new_node->keys, node->keys + mid + 1, right_count * sizeof(int32_t));
    std::memcpy(new_node->children, node->children + mid + 1,
                (right_count + 1) * sizeof(NodeBase*));
    new_node->count = right_count;

    for (uint16_t i = 0; i < right_count + 1; ++i) {
      if (new_node->children[i]) {
        new_node->children[i]->parent = new_node;
      }
    }

    node->count = mid;

    insertIntoParent(node, promote_key, new_node);
  }

  static size_t findChildIndex(const InternalNode* parent, const NodeBase* child) {
    const size_t limit = static_cast<size_t>(parent->count) + 1;
    for (size_t i = 0; i < limit; ++i) {
      if (parent->children[i] == child) return i;
    }
    return limit;
  }

  void updateMinKeyAfterInsert(NodeBase* node, int32_t new_min) {
    InternalNode* parent = node->parent;
    if (!parent) return;

    size_t index = findChildIndex(parent, node);
    if (index == 0) {
      updateMinKeyAfterInsert(parent, new_min);
      return;
    }
    parent->keys[index - 1] = new_min;
  }

  const LeafNode* leftmostNonEmpty() const {
    return nextNonEmpty(leftmost_);
  }

  const LeafNode* rightmostNonEmpty() const {
    return prevNonEmpty(rightmost_);
  }

  const LeafNode* nextNonEmpty(const LeafNode* leaf) const {
    const LeafNode* current = leaf;
    while (current && current->count == 0) {
      current = current->next;
    }
    return current;
  }

  const LeafNode* prevNonEmpty(const LeafNode* leaf) const {
    const LeafNode* current = leaf;
    while (current && current->count == 0) {
      current = current->prev;
    }
    return current;
  }

  int32_t firstKey(NodeBase* node) const {
    NodeBase* current = node;
    while (current && !current->leaf) {
      current = static_cast<InternalNode*>(current)->children[0];
    }
    if (!current) return 0;
    return static_cast<LeafNode*>(current)->keys[0];
  }

  void buildFromSorted(const std::vector<int32_t>& keys, const std::vector<LevelId>& values) {
    if (keys.empty()) {
      return;
    }

    std::vector<NodeBase*> level_nodes;
    level_nodes.reserve((keys.size() + kLeafMax - 1) / kLeafMax);

    LeafNode* prev_leaf = nullptr;
    size_t idx = 0;
    while (idx < keys.size()) {
      LeafNode* leaf = leaf_pool_.allocate();
      if (!leaf) break;
      const size_t remaining = keys.size() - idx;
      const uint16_t count = static_cast<uint16_t>(std::min<size_t>(kLeafMax, remaining));
      std::memcpy(leaf->keys, &keys[idx], count * sizeof(int32_t));
      std::memcpy(leaf->values, &values[idx], count * sizeof(LevelId));
      leaf->count = count;
      leaf->prev = prev_leaf;
      if (prev_leaf) {
        prev_leaf->next = leaf;
      }
      if (!leftmost_) {
        leftmost_ = leaf;
      }
      prev_leaf = leaf;
      level_nodes.push_back(leaf);
      idx += count;
    }

    rightmost_ = prev_leaf;

    if (level_nodes.empty()) {
      return;
    }

    while (level_nodes.size() > 1) {
      std::vector<NodeBase*> next_level;
      const size_t child_per_node = kInternalMax + 1;
      const size_t total = level_nodes.size();
      next_level.reserve((total + child_per_node - 1) / child_per_node);

      size_t child_idx = 0;
      while (child_idx < total) {
        InternalNode* node = internal_pool_.allocate();
        if (!node) break;
        const size_t remaining = total - child_idx;
        const size_t child_count = std::min(child_per_node, remaining);
        node->count = static_cast<uint16_t>(child_count - 1);
        for (size_t i = 0; i < child_count; ++i) {
          NodeBase* child = level_nodes[child_idx + i];
          node->children[i] = child;
          child->parent = node;
          if (i > 0) {
            node->keys[i - 1] = firstKey(child);
          }
        }
        next_level.push_back(node);
        child_idx += child_count;
      }

      level_nodes = std::move(next_level);
    }

    root_ = level_nodes[0];
    size_ = keys.size();
  }

  void maybeRebuild() {
    if (!auto_rebuild_) return;
    const size_t leaf_nodes = leaf_pool_.allocatedCount();
    if (leaf_nodes < 8) return;
    const size_t capacity = leaf_nodes * static_cast<size_t>(kLeafMax);
    if (capacity == 0) return;
    if (size_ * 3 < capacity && erases_since_rebuild_ > (capacity / 2)) {
      rebuild();
    }
  }

  NodeBase* root_ = nullptr;
  LeafNode* leftmost_ = nullptr;
  LeafNode* rightmost_ = nullptr;
  size_t size_ = 0;
  size_t inserts_since_rebuild_ = 0;
  size_t erases_since_rebuild_ = 0;
  bool auto_rebuild_ = true;

  NodePool<LeafNode> leaf_pool_;
  NodePool<InternalNode> internal_pool_;
};

} // namespace impl
