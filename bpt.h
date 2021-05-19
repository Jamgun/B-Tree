#pragma once

#ifndef UNIT_TEST
#include "predefined.h"
#else 
#include "util/unit_test_predefined.h"
#endif

namespace bpt {

// meta 在前，block 在后
#define OFFSET_META             0
#define OFFSET_BLOCK            OFFSET_META + sizeof(meta_t)
#define NODE_SIZE_NO_CHILDREN   sizeof(internal_node_t) - BPT_ORDER * sizeof(index_t)
#define LEAF_SIZE_NO_CHILDREN   sizeof(leaf_node_t) - BPT_ORDER * sizeof(record_t)

class BPlusTree {
public:
    // force_empty ：是否从空开始创建
    BPlusTree(const char* path, bool force_empty = false);
    ~BPlusTree();

    // 增删查改
    Status Insert(const key_t& key, const value_t& value);
    Status Search(const key_t& key, value_t* value);
    Status Remove(const key_t& key);

    meta_t GetMeta() const { return meta_; }

#ifndef UNIT_TEST
private:
#else
public:
#endif
    // init empty tree
    void InitFromEmpty();

    // create node without value
    template <typename T>
    void NodeCreate(off_t offset, T* node, T* new_node);

    // find parent
    off_t SearchParent(const key_t& key) const;
    // find leaf
    off_t SearchLeaf(off_t parent_off, const key_t& key) const;
    off_t SearchLeaf(const key_t& key) const {
        return SearchLeaf(SearchParent(key), key);
    }
    // insert index to node
    void Index2Node(off_t parent_off, const key_t& next_leaf_key, off_t leaf_off, off_t next_leaf_off);
    void IndexDirect2Node(internal_node_t* node, const key_t& key, off_t offset);
    // insert record to leaf without split 
    void RecordDirect2Leaf(leaf_node_t* leaf, const key_t& key, const value_t& value);
    // update parent of children
    void RenewParentOfChildren(index_t* begin, index_t* end, off_t parent);

    // Print
    std::string Print(bool bps = true) const;
    std::string PrintMeta() const;
    std::string PrintNode(off_t offset, const internal_node_t& node) const;
    std::string PrintLeaf(off_t offset, const leaf_node_t& node) const;

#ifndef UNIT_TEST
private:
#else
public:
#endif
    // open/close 文件，
    // fp_level 为打开次数，0 时 open，1 时 close
    void OpenFile(const char* mode = "rb+") const {
        if (fp_level_ == 0) 
            fp_ = fopen(path_, mode);
        fp_level_++;
    }
    void CloseFile() const {
        if (fp_level_ == 1)
            fclose(fp_);
        fp_level_--;
    }

    // alloc disk
    // slot 为 new block 的 off_t
    // 新增 size 大小的 off_t
    // 返回 new block 可用的 offset
    off_t Balloc(size_t size) {
        off_t slot = meta_.slot;
        meta_.slot += size;
        return slot;
    }
    // alloc disk
    // alloc leaf_node_t 大小的 disk
    off_t Balloc(leaf_node_t* leaf) {
        leaf->n = 0;
        meta_.leaf_node_num++;
        return Balloc(sizeof(leaf_node_t));
    }   
    // alloc disk
    // alloc internal_node_t 大小的 disk 
    off_t Balloc(internal_node_t* node) {
        node->n = 1;
        meta_.internal_node_num++;
        return Balloc(sizeof(internal_node_t));
    }
    // unalloc offset 位置的 internal_node_t
    // 仅进行 meta 变更
    void Bunalloc(internal_node_t* node, off_t offset) {
        meta_.internal_node_num--;
    }
    // unalloc offset 位置的 leaf_node_t
    // 仅进行 meta 变更
    void Bunalloc(leaf_node_t* leaf, off_t offset) {
        meta_.leaf_node_num--;
    }

    // 从距文件开头 offset 位移量为新的读写位置
    // 读入 1 次 size 大小的数据到 block 中
    // 成功返回 0，否则返回 -1
    int32_t Bread(void* block, off_t offset, size_t size) const {
        OpenFile();
        fseek(fp_, offset, SEEK_SET);
        size_t rd = fread(block, size, 1, fp_);
        CloseFile();
        return rd - 1;
    }
    template <typename T>
    int32_t Bread(T* block, off_t offset) const {
        return Bread(block, offset, sizeof(T));
    }
    // 从距文件开头 offset 位移量为新的读写位置
    // 写入 1 次 size 大小的 block 数据
    // 成功返回 0，否则返回 -1
    int32_t Bwrite(void* block, off_t offset, size_t size) const {
        OpenFile();
        fseek(fp_, offset, SEEK_SET);
        size_t wd = fwrite(block, size, 1, fp_);
        CloseFile();
        return wd - 1;
    }
    template <typename T>
    int32_t Bwrite(T* block, off_t offset) const {
        return Bwrite(block, offset, sizeof(T));
    }

#ifndef UNIT_TEST
private:
#else
public:
#endif
    char path_[512];
    meta_t meta_;

    mutable int32_t fp_level_;
    mutable FILE* fp_;

};

} // namespace bpt