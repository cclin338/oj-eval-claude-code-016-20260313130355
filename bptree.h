#ifndef BPTREE_H
#define BPTREE_H

#include <cstring>
#include <vector>
#include <cstdio>
#include <algorithm>

const int MAX_KEY_LEN = 64;
const int BLOCK_SIZE = 4096;
const int MAX_CHILDREN = 60;  // Tuned for performance

struct Key {
    char data[MAX_KEY_LEN + 1];

    Key() { memset(data, 0, sizeof(data)); }

    Key(const char* s) {
        memset(data, 0, sizeof(data));
        if (s) strncpy(data, s, MAX_KEY_LEN);
    }

    int compare(const Key& o) const { return strcmp(data, o.data); }

    bool operator<(const Key& o) const { return compare(o) < 0; }
    bool operator<=(const Key& o) const { return compare(o) <= 0; }
    bool operator>(const Key& o) const { return compare(o) > 0; }
    bool operator>=(const Key& o) const { return compare(o) >= 0; }
    bool operator==(const Key& o) const { return compare(o) == 0; }
};

struct KeyValue {
    Key key;
    int value;

    KeyValue() : value(0) {}
    KeyValue(const Key& k, int v) : key(k), value(v) {}

    bool operator<(const KeyValue& o) const {
        int cmp = key.compare(o.key);
        return cmp < 0 || (cmp == 0 && value < o.value);
    }

    bool operator==(const KeyValue& o) const {
        return key == o.key && value == o.value;
    }
};

struct Node {
    bool is_leaf;
    int count;
    int children[MAX_CHILDREN];  // For internal nodes
    KeyValue data[MAX_CHILDREN - 1];  // Actual data
    Key keys[MAX_CHILDREN - 1];  // Routing keys for internal nodes
    int next;  // For leaf nodes

    Node() : is_leaf(true), count(0), next(-1) {
        for (int i = 0; i < MAX_CHILDREN; i++) children[i] = -1;
    }
};

class BPTree {
private:
    FILE* file;
    std::string filename;
    int root_offset;
    int node_count;

    void writeHeader() {
        fseek(file, 0, SEEK_SET);
        fwrite(&root_offset, sizeof(int), 1, file);
        fwrite(&node_count, sizeof(int), 1, file);
        fflush(file);
    }

    void readHeader() {
        fseek(file, 0, SEEK_SET);
        size_t r1 = fread(&root_offset, sizeof(int), 1, file);
        size_t r2 = fread(&node_count, sizeof(int), 1, file);
        (void)r1; (void)r2;
    }

    int allocNode() {
        return node_count++;
    }

    void writeNode(int offset, const Node& node) {
        fseek(file, sizeof(int) * 2 + offset * sizeof(Node), SEEK_SET);
        fwrite(&node, sizeof(Node), 1, file);
        fflush(file);
    }

    void readNode(int offset, Node& node) {
        fseek(file, sizeof(int) * 2 + offset * sizeof(Node), SEEK_SET);
        size_t r = fread(&node, sizeof(Node), 1, file);
        (void)r;
    }

    int findLeaf(const Key& key) {
        if (root_offset == -1) return -1;

        int current = root_offset;
        Node node;

        while (true) {
            readNode(current, node);
            if (node.is_leaf) return current;

            int i = 0;
            while (i < node.count - 1 && key >= node.keys[i]) i++;
            current = node.children[i];
        }
    }

    void insertIntoLeaf(int offset, const KeyValue& kv) {
        Node node;
        readNode(offset, node);

        // Check for duplicate
        for (int i = 0; i < node.count; i++) {
            if (node.data[i] == kv) return;
        }

        // Find position and insert
        int i = node.count - 1;
        while (i >= 0 && node.data[i].key > kv.key ||
               (node.data[i].key == kv.key && node.data[i].value > kv.value)) {
            node.data[i + 1] = node.data[i];
            i--;
        }
        node.data[i + 1] = kv;
        node.count++;
        writeNode(offset, node);
    }

    void splitLeaf(int offset, KeyValue& promoteKey, int& newLeafOffset) {
        Node oldLeaf;
        readNode(offset, oldLeaf);

        int mid = oldLeaf.count / 2;

        Node newLeaf;
        newLeaf.is_leaf = true;
        newLeaf.count = oldLeaf.count - mid;
        for (int i = 0; i < newLeaf.count; i++) {
            newLeaf.data[i] = oldLeaf.data[mid + i];
        }

        newLeaf.next = oldLeaf.next;
        oldLeaf.count = mid;

        newLeafOffset = allocNode();
        oldLeaf.next = newLeafOffset;

        promoteKey = newLeaf.data[0];

        writeNode(offset, oldLeaf);
        writeNode(newLeafOffset, newLeaf);
    }

    void splitInternal(int offset, KeyValue& promoteKey, int& newNodeOffset) {
        Node oldNode;
        readNode(offset, oldNode);

        int mid = oldNode.count / 2;

        Node newNode;
        newNode.is_leaf = false;
        newNode.count = oldNode.count - mid;

        for (int i = 0; i < newNode.count - 1; i++) {
            newNode.keys[i] = oldNode.keys[mid + i];
        }
        for (int i = 0; i < newNode.count; i++) {
            newNode.children[i] = oldNode.children[mid + i];
        }

        promoteKey.key = oldNode.keys[mid - 1];
        oldNode.count = mid;

        newNodeOffset = allocNode();

        writeNode(offset, oldNode);
        writeNode(newNodeOffset, newNode);
    }

    bool insertRecursive(int offset, const KeyValue& kv, KeyValue& promoteKey, int& newChildOffset) {
        Node node;
        readNode(offset, node);

        if (node.is_leaf) {
            if (node.count < MAX_CHILDREN - 1) {
                insertIntoLeaf(offset, kv);
                return false;
            } else {
                insertIntoLeaf(offset, kv);
                splitLeaf(offset, promoteKey, newChildOffset);
                return true;
            }
        } else {
            // Find which child to descend to
            int i = 0;
            while (i < node.count - 1 && kv.key >= node.keys[i]) i++;

            KeyValue childPromote;
            int childNewOffset;
            bool needSplit = insertRecursive(node.children[i], kv, childPromote, childNewOffset);

            if (!needSplit) return false;

            // Insert promoted key into current internal node
            if (node.count < MAX_CHILDREN) {
                // Insert without splitting
                for (int j = node.count - 1; j > i; j--) {
                    node.keys[j] = node.keys[j - 1];
                }
                for (int j = node.count; j > i + 1; j--) {
                    node.children[j] = node.children[j - 1];
                }
                node.keys[i] = childPromote.key;
                node.children[i + 1] = childNewOffset;
                node.count++;
                writeNode(offset, node);
                return false;
            } else {
                // Need to split internal node
                for (int j = node.count - 1; j > i; j--) {
                    node.keys[j] = node.keys[j - 1];
                }
                for (int j = node.count; j > i + 1; j--) {
                    node.children[j] = node.children[j - 1];
                }
                node.keys[i] = childPromote.key;
                node.children[i + 1] = childNewOffset;
                node.count++;

                writeNode(offset, node);
                splitInternal(offset, promoteKey, newChildOffset);
                return true;
            }
        }
    }

public:
    BPTree(const std::string& fname) : filename(fname), root_offset(-1), node_count(0) {
        file = fopen(filename.c_str(), "rb+");

        if (file) {
            readHeader();
        } else {
            file = fopen(filename.c_str(), "wb+");
            writeHeader();
        }
    }

    ~BPTree() {
        if (file) {
            writeHeader();
            fclose(file);
        }
    }

    void insert(const char* key_str, int value) {
        KeyValue kv(Key(key_str), value);

        if (root_offset == -1) {
            root_offset = allocNode();
            Node root;
            root.is_leaf = true;
            root.count = 1;
            root.data[0] = kv;
            writeNode(root_offset, root);
            writeHeader();
            return;
        }

        KeyValue promoteKey;
        int newChildOffset;
        bool needSplit = insertRecursive(root_offset, kv, promoteKey, newChildOffset);

        if (needSplit) {
            int oldRoot = root_offset;
            root_offset = allocNode();

            Node newRoot;
            newRoot.is_leaf = false;
            newRoot.count = 2;
            newRoot.keys[0] = promoteKey.key;
            newRoot.children[0] = oldRoot;
            newRoot.children[1] = newChildOffset;

            writeNode(root_offset, newRoot);
            writeHeader();
        }
    }

    void find(const char* key_str, std::vector<int>& result) {
        result.clear();
        if (root_offset == -1) return;

        Key key(key_str);
        int leaf_off = findLeaf(key);
        if (leaf_off == -1) return;

        // Scan through linked leaf nodes
        while (leaf_off != -1) {
            Node node;
            readNode(leaf_off, node);

            for (int i = 0; i < node.count; i++) {
                if (node.data[i].key == key) {
                    result.push_back(node.data[i].value);
                } else if (node.data[i].key > key) {
                    // Keys are sorted, so we're past our target
                    std::sort(result.begin(), result.end());
                    return;
                }
            }

            leaf_off = node.next;
        }

        std::sort(result.begin(), result.end());
    }

    void remove(const char* key_str, int value) {
        if (root_offset == -1) return;

        Key key(key_str);
        KeyValue target(key, value);
        int leaf_off = findLeaf(key);
        if (leaf_off == -1) return;

        Node node;
        readNode(leaf_off, node);

        int idx = -1;
        for (int i = 0; i < node.count; i++) {
            if (node.data[i] == target) {
                idx = i;
                break;
            }
        }

        if (idx == -1) return;

        for (int i = idx; i < node.count - 1; i++) {
            node.data[i] = node.data[i + 1];
        }
        node.count--;

        writeNode(leaf_off, node);
    }
};

#endif
