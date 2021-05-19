#include "bpt.h"

#include <algorithm>
#include <deque>
#include <iostream>

namespace bpt {

OPERATOR_KEYCMP(index_t);
OPERATOR_KEYCMP(record_t);

template <typename T>
inline typename T::child_t begin(T& node) {
    return node.children;
}
template <typename T>
inline typename T::child_t end(T& node) {
    return node.children + node.n;
}

BPlusTree::BPlusTree(const char* path, bool force_empty) 
    : fp_level_(0),
    fp_(nullptr) {
    // copy path
    bzero(path_, sizeof(path_));
    strcpy(path_, path);
    // recover prev meta
    if (!force_empty) {
        if (Bread(&meta_, OFFSET_META))
            force_empty = true;
    }
    // create new tree
    if (force_empty) {
        OpenFile("w+");
        InitFromEmpty();
        CloseFile();
    }
}

BPlusTree::~BPlusTree() {}

Status BPlusTree::Insert(const key_t& key, const value_t& value) {
    std::cout << "Insert key : " << key << "\tvalue : " << value << "\t";
    off_t parent_off = SearchParent(key);
    std::cout << "parent_off : " << parent_off << "\t";
    off_t leaf_off = SearchLeaf(parent_off, key);
    std::cout << "leaf_off : " << leaf_off << "\n";
    leaf_node_t leaf;
    Bread(&leaf, leaf_off);

    // check the same key
    if (std::binary_search(begin(leaf), end(leaf), key)) {
        return Status(Status::EXIST);
    }

    // split when full
    if (leaf.n == meta_.order) {
        std::cout << "leaf full : " << leaf.n << "\n";
        // new sibling leaf
        leaf_node_t new_leaf;
        NodeCreate(leaf_off, &leaf, &new_leaf);

        // find mid
        size_t mid = leaf.n / 2;
        bool right = keycmp(key, leaf.children[mid].key) > 0;
        if (right) mid++;

        // split, pay attention to the index
        std::copy(leaf.children + mid, end(leaf), begin(new_leaf));
        new_leaf.n = leaf.n - mid;
        leaf.n = mid;

        // insert kv
        if (right) RecordDirect2Leaf(&new_leaf, key, value);
        else RecordDirect2Leaf(&leaf, key, value);

        // persist
        Bwrite(&leaf, leaf_off);
        Bwrite(&new_leaf, leaf.next);

        //InsertIndex
        Index2Node(parent_off, new_leaf.children[0].key, leaf_off, leaf.next);
    } else {
        RecordDirect2Leaf(&leaf, key, value);
        Bwrite(&leaf, leaf_off);
    }
    return Status();
}

template <typename T>
void BPlusTree::NodeCreate(off_t offset, T* node, T* new_node) {
    new_node->parent = node->parent;
    // 需要更改4个指针
    new_node->next   = node->next;
    new_node->prev   = offset;
    // 磁盘上申请空间
    node->next       = Balloc(new_node);
    // 仅更改 next node's prev
    if (new_node->next != 0) {
        T next_node;
        Bread(&next_node, new_node->next, LEAF_SIZE_NO_CHILDREN);
        next_node.prev = node->next;
        Bwrite(&next_node, new_node->next, LEAF_SIZE_NO_CHILDREN);
    }
    // 持久化meta
    Bwrite(&meta_, OFFSET_META);
}

void BPlusTree::Index2Node(off_t parent_off, const key_t& next_leaf_key, off_t leaf_off, off_t next_leaf_off) {
    //new root
    if (parent_off == 0) {
        internal_node_t root;
        root.next = root.prev = root.parent = 0;
        meta_.root_offset = Balloc(&root);
        meta_.height ++;

        root.n = 2;
        root.children[0].key = next_leaf_key;
        root.children[0].offset = leaf_off;
        root.children[1].offset = next_leaf_off;

        Bwrite(&meta_, OFFSET_META);
        Bwrite(&root, meta_.root_offset);

        RenewParentOfChildren(begin(root), end(root), meta_.root_offset);
        return;
    }

    internal_node_t parent;
    Bread(&parent, parent_off);

    // split when full
    if (parent.n == meta_.order) {
        std::cout << "node full : " << parent.n << "\n";
        // new sibling node
        internal_node_t next_node;
        NodeCreate(parent_off, &parent, &next_node);

        // find mid, a[mid] 为左边的数据, a[mid + 1] 为右边的数据
        // 插入 48 到 |42|45|6| | 时需要分裂
        // mid 在 45，48 > 45 可能在右侧，右移后, mid 在 6，发现 48 < 6 ，一定在左侧，与right不符
        // 这样会更不平均，恢复原来的 mid 在 45，插入在右侧
        size_t mid = (parent.n - 1) / 2;
        bool right = keycmp(next_leaf_key, parent.children[mid].key) > 0;
        if (right) mid++;
        if (right && keycmp(next_leaf_key, parent.children[mid].key) < 0) mid--;
        key_t mid_key = parent.children[mid].key;

        // split, pay attention to the index, parent[mid + 1], No.(mid + 2)
        std::copy(parent.children + mid + 1, end(parent), begin(next_node));
        next_node.n = parent.n - mid - 1;
        parent.n = mid + 1;

        // insert kv
        if (right) IndexDirect2Node(&next_node, next_leaf_key, next_leaf_off);
        else IndexDirect2Node(&parent, next_leaf_key, next_leaf_off);

        // persist
        Bwrite(&parent, parent_off);
        Bwrite(&next_node, parent.next);

        // update children
        RenewParentOfChildren(begin(next_node), end(next_node), parent.next);


        // InsertIndex
        // mid_key 存疑
        Index2Node(parent.parent, mid_key, parent_off, parent.next);
    } else {
        IndexDirect2Node(&parent, next_leaf_key, next_leaf_off);
        Bwrite(&parent, parent_off);
    }
}

void BPlusTree::RenewParentOfChildren(index_t* begin, index_t* end, off_t parent) {
    internal_node_t node;
    // 不包含 end
    while (begin != end) {
        Bread(&node, begin->offset, NODE_SIZE_NO_CHILDREN);
        node.parent = parent;
        Bwrite(&node, begin->offset, NODE_SIZE_NO_CHILDREN);
        begin++;
    }
}

void BPlusTree::IndexDirect2Node(internal_node_t* node, const key_t& key, off_t offset) {
    // 包含end, 找更大的
    index_t* next_index = std::upper_bound(begin(*node), end(*node) - 1, key);
    std::cout << "node rank : " << next_index - begin(*node) << "\n";
    std::copy_backward(next_index, end(*node), end(*node) + 1);
    next_index->key = key;
    // key - index : 表示 [prev_key, key)
    // 之前的排列
    // key0 - index0 : [0, key0) 
    // key1 - index1 : [key0, key1) 
    // key2 - index2 : [key1, key2)
    // 分裂之后, 从[key1, key2) 分裂出一个key1.5，形成下面的结构
    // key0   - index0   : [0, key0) 
    // key1   - index1   : [key0, key1)
    // key1.5 - index1.5 : [key1, key1.5)  
    // key2   - index2   : [key1.5, key2)
    // 此时 [key1, key2) 块的内容变成了 [key1, key1.5), index不变
    // 因此新key对应的index反而是旧index
    // 后面的key对应的index反而是新index
    // 注意 null - index : [prev_key, null] 对应的是上限
    next_index->offset = (next_index + 1)->offset;
    (next_index + 1)->offset = offset;
    node->n++;
}

void BPlusTree::RecordDirect2Leaf(leaf_node_t* leaf, const key_t& key, const value_t& value) {
    // 包含end, 找更大的
    record_t* next_record = std::upper_bound(begin(*leaf), end(*leaf), key);
    std::cout << "leaf rank : " << next_record - begin(*leaf) << "\n";
    // 不包含 end
    std::copy_backward(next_record, end(*leaf), end(*leaf) + 1);
    next_record->key = key;
    next_record->value = value;
    leaf->n++; 
}

off_t BPlusTree::SearchParent(const key_t& key) const {
    off_t parent_off = meta_.root_offset;
    // height 不包含 leaf
    int32_t height = meta_.height;
    while (height-- > 1) {
        internal_node_t node;
        Bread(&node, parent_off);
        index_t* parent_next = std::upper_bound(begin(node), end(node) - 1, key);
        parent_off = parent_next->offset;
    }
    return parent_off;
}

off_t BPlusTree::SearchLeaf(off_t parent_off, const key_t& key) const {
    internal_node_t node;
    Bread(&node, parent_off);
    index_t* leaf = std::upper_bound(begin(node), end(node) - 1, key);
    return leaf->offset;
}

void BPlusTree::InitFromEmpty() {
    bzero(&meta_, sizeof(meta_t));
    meta_.order = BPT_ORDER;
    meta_.key_size = sizeof(key_t);
    meta_.value_size = sizeof(value_t);
    meta_.height = 1;
    meta_.slot = OFFSET_BLOCK;
    // new root node
    internal_node_t root;
    root.next = root.prev = root.parent = 0;
    meta_.root_offset = Balloc(&root);
    // new first leaf node
    leaf_node_t leaf;
    leaf.next = leaf.prev = 0;
    leaf.parent = meta_.root_offset;
    meta_.leaf_offset = root.children[0].offset = Balloc(&leaf);

    // write data
    Bwrite(&meta_, OFFSET_META);
    Bwrite(&root, meta_.root_offset);
    Bwrite(&leaf, meta_.leaf_offset);
}

#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define RESET   "\033[0m"

std::string BPlusTree::PrintMeta() const {
    std::cout << RED << "meta\n";
    std::cout << RESET << "order     : \t" << meta_.order
              << "\nkey_size  : \t" << meta_.key_size
              << "\nval_size  : \t" << meta_.value_size
              << "\nmeta_size : \t" << sizeof(meta_t)
              << "\nnode_size : \t" << sizeof(internal_node_t)
              << "\nleaf_size : \t" << sizeof(leaf_node_t)
              << "\nnode_num  : \t" << meta_.internal_node_num
              << "\nleaf_num  : \t" << meta_.leaf_node_num
              << "\nheight    : \t" << meta_.height
              << "\nroot_off  : \t" << meta_.root_offset
              << "\nleaf_off  : \t" << meta_.leaf_offset
              << "\nnew_slot  : \t" << meta_.slot
              << "\n\n";
    return "";
}

std::string BPlusTree::PrintNode(off_t offset, const internal_node_t& node) const {
    std::cout << GREEN << "node(" << node.n << "): " << offset << "\n";
    std::cout << RESET << "parent : " << node.parent 
              << "\tprev : " << node.prev 
              << "\tnext : " << node.next 
              << "\n";
    for (size_t i = 0; i < node.n; i++) {
        std::cout << "child  : " << i 
                  << "\tkey  : " << node.children[i].key 
                  << "\toff  : " << node.children[i].offset 
                  << "\n";
    }
    std::cout << "\n";
    return "";
}

std::string BPlusTree::PrintLeaf(off_t offset, const leaf_node_t& leaf) const {
    std::cout << GREEN << "leaf(" << leaf.n  << "): " << offset << "\n";
    std::cout << RESET << "parent : " << leaf.parent 
              << "\tprev : " << leaf.prev 
              << "\tnext : " << leaf.next 
              << "\n";
    for (size_t i = 0; i < leaf.n; i++) {
        std::cout << "child  : " << i 
                  << "\tkey  : " << leaf.children[i].key 
                  << "\tval  : " << leaf.children[i].value 
                  << "\n";
    }
    std::cout << "\n";
    return "";
}

std::string BPlusTree::Print(bool bfs) const {
    PrintMeta();
    // height 不包含 leaf
    int32_t height = meta_.height;
    std::deque<off_t> node_deque;
    node_deque.push_back(meta_.root_offset);
    while (height-- > 0) {
        std::cout << RED << "height : " << height + 1 << "\n";
        std::deque<off_t> next_node_deque;
        while(node_deque.size()) {
            off_t offset = node_deque.front();
            internal_node_t node;
            Bread(&node, offset);
            PrintNode(offset, node);
            for (size_t i = 0; i < node.n; i++) {
                next_node_deque.push_back(node.children[i].offset);
            }
            node_deque.pop_front();
        }
        node_deque = next_node_deque;
        next_node_deque.clear();   
    }
    std::cout << RED << "leaf\n";
    while(node_deque.size()) {
        off_t offset = node_deque.front();
        leaf_node_t leaf;
        Bread(&leaf, offset);
        PrintLeaf(offset, leaf);
        node_deque.pop_front();
    }
    return "";
}

} // namespace bpt