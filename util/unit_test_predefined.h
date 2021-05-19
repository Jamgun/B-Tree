#pragma once

#include <string>
#include <ostream>

namespace bpt {

#define BPT_ORDER 4

// BPT 的 meta 信息
struct meta_t {
    size_t order;               // BPT 的 order

    size_t key_size;            // key 的 size
    size_t value_size;          // value 的 size

    size_t internal_node_num;   // internal node 的 num
    size_t leaf_node_num;       // leaf node 的 num
    size_t height;              // 不包含 leaf
    
    off_t slot;                 // new block 的 offset
    off_t root_offset;          // root block 的 offset
    off_t leaf_offset;          // leaf block 的 offset
};

// real 的 k/v
struct key_t {
    char k[16];
    key_t(const char* str = "") {
        bzero(k, sizeof(k));
        strcpy(k, str);
    }

    operator bool() const {
        return strcmp(k, "");
    }
};
typedef int32_t value_t;

inline std::ostream& operator << (std::ostream& out, const key_t& key) {
    out << key.k;
    return out;
}

// compare
inline int keycmp(const key_t& a, const key_t& b) {
    int x = strlen(a.k) - strlen(b.k);
    return x == 0 ? strcmp(a.k, b.k) : x;
}

#define OPERATOR_KEYCMP(type) \
    bool operator < (const key_t& k, const type& t) { \
        return keycmp(k, t.key) < 0; \
    } \
    bool operator < (const type& t, const key_t& k) { \
        return keycmp(t.key, k) < 0; \
    } \
    bool operator == (const key_t& k, const type& t) { \
        return keycmp(k, t.key) == 0; \
    } \
    bool operator == (const type& t, const key_t& k) { \
        return keycmp(t.key, k) == 0; \
    }

// internal node 的 children 的 index 信息
struct index_t {
    key_t key;
    off_t offset;
};
// internal node 的 block 信息
struct internal_node_t {
    typedef index_t* child_t;

    off_t parent;

    off_t next;
    off_t prev;

    size_t n;
    index_t children[BPT_ORDER];
};

// leaf node 的 children 的 record 信息
struct record_t {
    key_t key;
    value_t value;
};
// leaf node 的 block 信息
struct leaf_node_t {
    typedef record_t* child_t;

    off_t parent;

    off_t next;
    off_t prev;

    size_t n;
    record_t children[BPT_ORDER];
};

// status code
struct Status {
    enum {
        SUCCESS = 0,    //成功
        EXIST = 1,
        FAIL = 101,     //待定义
    };
    bool OK() {return code == SUCCESS;}
    bool Exist() {return code == EXIST;}
    Status() : code(0) {}
    explicit Status(int c) : code(c) {}
    int code;
};

} //namespace