#ifndef BPTREE_H
#define BPTREE_H

#include <cstring>
#include <fstream>
#include <vector>
#include <cstdio>
#include <algorithm>

const int MAX_KEY_LEN = 64;
const int BLOCK_SIZE = 4096;
const int MAX_KEYS = 50;  // Tuned for performance

struct Key {
    char data[MAX_KEY_LEN + 1];

    Key() { memset(data, 0, sizeof(data)); }

    Key(const char* s) {
        memset(data, 0, sizeof(data));
        if (s) strncpy(data, s, MAX_KEY_LEN);
    }

    bool operator<(const Key& o) const { return strcmp(data, o.data) < 0; }
    bool operator<=(const Key& o) const { return strcmp(data, o.data) <= 0; }
    bool operator>(const Key& o) const { return strcmp(data, o.data) > 0; }
    bool operator>=(const Key& o) const { return strcmp(data, o.data) >= 0; }
    bool operator==(const Key& o) const { return strcmp(data, o.data) == 0; }
};

struct KeyValue {
    Key key;
    int value;

    KeyValue() : value(0) {}
    KeyValue(const Key& k, int v) : key(k), value(v) {}

    bool operator<(const KeyValue& o) const {
        int cmp = strcmp(key.data, o.key.data);
        return cmp < 0 || (cmp == 0 && value < o.value);
    }

    bool operator==(const KeyValue& o) const {
        return key == o.key && value == o.value;
    }

    bool operator>(const KeyValue& o) const {
        int cmp = strcmp(key.data, o.key.data);
        return cmp > 0 || (cmp == 0 && value > o.value);
    }
};

struct Node {
    bool is_leaf;
    int count;
    int parent;
    int children[MAX_KEYS + 1];
    KeyValue data[MAX_KEYS];
    int next;  // For leaf nodes

    Node() : is_leaf(true), count(0), parent(-1), next(-1) {
        for (int i = 0; i <= MAX_KEYS; i++) children[i] = -1;
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
        fread(&root_offset, sizeof(int), 1, file);
        fread(&node_count, sizeof(int), 1, file);
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
        fread(&node, sizeof(Node), 1, file);
    }

    int findChild(Node& node, const KeyValue& kv) {
        int i = 0;
        while (i < node.count && kv.key >= node.data[i].key) i++;
        return i;
    }

    int findLeaf(const Key& key) {
        if (root_offset == -1) return -1;

        int current = root_offset;
        Node node;

        while (true) {
            readNode(current, node);
            if (node.is_leaf) return current;

            int i = 0;
            while (i < node.count && key >= node.data[i].key) i++;
            current = node.children[i];
        }
    }

    void insertNonFull(int offset, const KeyValue& kv) {
        Node node;
        readNode(offset, node);

        if (node.is_leaf) {
            // Check for duplicate
            for (int i = 0; i < node.count; i++) {
                if (node.data[i] == kv) return;
            }

            int i = node.count - 1;
            while (i >= 0 && node.data[i] > kv) {
                node.data[i + 1] = node.data[i];
                i--;
            }
            node.data[i + 1] = kv;
            node.count++;
            writeNode(offset, node);
        } else {
            int i = findChild(node, kv);
            Node child;
            readNode(node.children[i], child);

            if (child.count == MAX_KEYS) {
                splitChild(offset, i);
                readNode(offset, node);
                if (kv.key >= node.data[i].key) i++;
            }
            insertNonFull(node.children[i], kv);
        }
    }

    void splitChild(int parent_off, int index) {
        Node parent;
        readNode(parent_off, parent);

        Node full_node;
        readNode(parent.children[index], full_node);

        int mid = full_node.count / 2;
        Node new_node;
        new_node.is_leaf = full_node.is_leaf;
        new_node.parent = parent_off;

        if (full_node.is_leaf) {
            new_node.count = full_node.count - mid;
            for (int i = 0; i < new_node.count; i++) {
                new_node.data[i] = full_node.data[mid + i];
            }
            full_node.count = mid;
            new_node.next = full_node.next;
            full_node.next = node_count;
        } else {
            new_node.count = full_node.count - mid - 1;
            for (int i = 0; i < new_node.count; i++) {
                new_node.data[i] = full_node.data[mid + 1 + i];
                new_node.children[i] = full_node.children[mid + 1 + i];
            }
            new_node.children[new_node.count] = full_node.children[full_node.count];
            full_node.count = mid;
        }

        int new_offset = allocNode();

        for (int i = parent.count; i > index; i--) {
            parent.data[i] = parent.data[i - 1];
            parent.children[i + 1] = parent.children[i];
        }

        parent.data[index] = full_node.is_leaf ? new_node.data[0] : full_node.data[mid];
        parent.children[index + 1] = new_offset;
        parent.count++;

        writeNode(parent_off, parent);
        writeNode(parent.children[index], full_node);
        writeNode(new_offset, new_node);
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

        Node root;
        readNode(root_offset, root);

        if (root.count == MAX_KEYS) {
            int new_root_off = allocNode();
            Node new_root;
            new_root.is_leaf = false;
            new_root.count = 0;
            new_root.children[0] = root_offset;
            writeNode(new_root_off, new_root);

            root_offset = new_root_off;
            splitChild(root_offset, 0);
            writeHeader();
        }

        insertNonFull(root_offset, kv);
    }

    void find(const char* key_str, std::vector<int>& result) {
        result.clear();
        if (root_offset == -1) return;

        Key key(key_str);
        int leaf_off = findLeaf(key);
        if (leaf_off == -1) return;

        while (leaf_off != -1) {
            Node node;
            readNode(leaf_off, node);

            bool found_any = false;
            for (int i = 0; i < node.count; i++) {
                if (node.data[i].key == key) {
                    result.push_back(node.data[i].value);
                    found_any = true;
                } else if (found_any || node.data[i].key > key) {
                    break;
                }
            }

            if (!found_any) break;
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
