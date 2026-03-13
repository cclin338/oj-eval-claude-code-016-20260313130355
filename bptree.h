#ifndef BPTREE_H
#define BPTREE_H

#include <cstring>
#include <vector>
#include <cstdio>
#include <algorithm>

const int MAX_KEY_LEN = 64;
const int ORDER = 100;  // B+ tree order - max keys per node

struct KeyValue {
    char key[MAX_KEY_LEN + 1];
    int value;

    KeyValue() : value(0) {
        memset(key, 0, sizeof(key));
    }

    KeyValue(const char* k, int v) : value(v) {
        memset(key, 0, sizeof(key));
        if (k) strncpy(key, k, MAX_KEY_LEN);
    }

    int keycmp(const KeyValue& o) const {
        return strcmp(key, o.key);
    }

    bool operator<(const KeyValue& o) const {
        int c = strcmp(key, o.key);
        return c < 0 || (c == 0 && value < o.value);
    }

    bool operator==(const KeyValue& o) const {
        return strcmp(key, o.key) == 0 && value == o.value;
    }

    bool operator>(const KeyValue& o) const {
        int c = strcmp(key, o.key);
        return c > 0 || (c == 0 && value > o.value);
    }
};

struct Node {
    bool is_leaf;
    int count;
    int parent;
    KeyValue entries[ORDER];
    int children[ORDER + 1];
    int next;  // For linking leaf nodes

    Node() : is_leaf(true), count(0), parent(-1), next(-1) {
        for (int i = 0; i <= ORDER; i++) children[i] = -1;
    }
};

class BPTree {
private:
    FILE* file;
    std::string filename;
    int root_offset;
    int node_count;

    void writeHeader() {
        rewind(file);
        fwrite(&root_offset, sizeof(int), 1, file);
        fwrite(&node_count, sizeof(int), 1, file);
        fflush(file);
    }

    void readHeader() {
        rewind(file);
        size_t r1 = fread(&root_offset, sizeof(int), 1, file);
        size_t r2 = fread(&node_count, sizeof(int), 1, file);
        (void)r1; (void)r2;
    }

    int allocNode() {
        return node_count++;
    }

    long getNodePos(int offset) {
        return sizeof(int) * 2 + (long)offset * sizeof(Node);
    }

    void writeNode(int offset, const Node& node) {
        fseek(file, getNodePos(offset), SEEK_SET);
        fwrite(&node, sizeof(Node), 1, file);
        fflush(file);
    }

    void readNode(int offset, Node& node) {
        fseek(file, getNodePos(offset), SEEK_SET);
        size_t r = fread(&node, sizeof(Node), 1, file);
        (void)r;
    }

    int findLeafNode(const char* key_str) {
        if (root_offset == -1) return -1;

        KeyValue target(key_str, 0);
        int current = root_offset;

        while (true) {
            Node node;
            readNode(current, node);

            if (node.is_leaf) {
                return current;
            }

            // Find the appropriate child
            int i = 0;
            while (i < node.count && target.keycmp(node.entries[i]) >= 0) {
                i++;
            }
            current = node.children[i];
        }
    }

    void splitChild(int parent_idx, int child_idx) {
        Node parent;
        readNode(parent_idx, parent);

        Node left;
        readNode(parent.children[child_idx], left);

        int right_idx = allocNode();
        Node right;
        right.is_leaf = left.is_leaf;
        right.parent = parent_idx;

        int mid = left.count / 2;

        if (left.is_leaf) {
            // For leaf nodes, keep first half in left, second half in right
            right.count = left.count - mid;
            for (int i = 0; i < right.count; i++) {
                right.entries[i] = left.entries[mid + i];
            }
            left.count = mid;

            // Link leaf nodes
            right.next = left.next;
            left.next = right_idx;

            // Promote first key of right child to parent
            KeyValue promote_key = right.entries[0];

            // Insert into parent
            for (int i = parent.count; i > child_idx; i--) {
                parent.entries[i] = parent.entries[i - 1];
                parent.children[i + 1] = parent.children[i];
            }
            parent.entries[child_idx] = promote_key;
            parent.children[child_idx + 1] = right_idx;
            parent.count++;

        } else {
            // For internal nodes
            right.count = left.count - mid - 1;
            for (int i = 0; i < right.count; i++) {
                right.entries[i] = left.entries[mid + 1 + i];
                right.children[i] = left.children[mid + 1 + i];
            }
            right.children[right.count] = left.children[left.count];

            KeyValue promote_key = left.entries[mid];
            left.count = mid;

            // Insert into parent
            for (int i = parent.count; i > child_idx; i--) {
                parent.entries[i] = parent.entries[i - 1];
                parent.children[i + 1] = parent.children[i];
            }
            parent.entries[child_idx] = promote_key;
            parent.children[child_idx + 1] = right_idx;
            parent.count++;
        }

        writeNode(parent_idx, parent);
        writeNode(parent.children[child_idx], left);
        writeNode(right_idx, right);
    }

    void insertNonFull(int node_idx, const KeyValue& kv) {
        Node node;
        readNode(node_idx, node);

        if (node.is_leaf) {
            // Check for duplicate
            for (int i = 0; i < node.count; i++) {
                if (node.entries[i] == kv) return;
            }

            // Insert into sorted position
            int i = node.count - 1;
            while (i >= 0 && node.entries[i] > kv) {
                node.entries[i + 1] = node.entries[i];
                i--;
            }
            node.entries[i + 1] = kv;
            node.count++;
            writeNode(node_idx, node);
        } else {
            // Find child to insert into
            int i = 0;
            while (i < node.count && kv.keycmp(node.entries[i]) >= 0) {
                i++;
            }

            Node child;
            readNode(node.children[i], child);

            if (child.count >= ORDER) {
                splitChild(node_idx, i);
                // Re-read node after split
                readNode(node_idx, node);
                // Determine which child to go to now
                if (kv.keycmp(node.entries[i]) >= 0) {
                    i++;
                }
            }

            insertNonFull(node.children[i], kv);
        }
    }

public:
    BPTree(const std::string& fname) : filename(fname), root_offset(-1), node_count(0) {
        file = fopen(filename.c_str(), "rb+");

        if (file) {
            readHeader();
        } else {
            file = fopen(filename.c_str(), "wb+");
            if (file) writeHeader();
        }
    }

    ~BPTree() {
        if (file) {
            writeHeader();
            fclose(file);
        }
    }

    void insert(const char* key_str, int value) {
        KeyValue kv(key_str, value);

        if (root_offset == -1) {
            root_offset = allocNode();
            Node root;
            root.is_leaf = true;
            root.count = 1;
            root.entries[0] = kv;
            writeNode(root_offset, root);
            writeHeader();
            return;
        }

        Node root;
        readNode(root_offset, root);

        if (root.count >= ORDER) {
            // Root is full, need to create new root
            int new_root_idx = allocNode();
            Node new_root;
            new_root.is_leaf = false;
            new_root.count = 0;
            new_root.children[0] = root_offset;

            writeNode(new_root_idx, new_root);

            root_offset = new_root_idx;
            splitChild(new_root_idx, 0);
            writeHeader();
        }

        insertNonFull(root_offset, kv);
    }

    void find(const char* key_str, std::vector<int>& result) {
        result.clear();
        if (root_offset == -1) return;

        int leaf_idx = findLeafNode(key_str);
        if (leaf_idx == -1) return;

        // Scan through linked leaf nodes
        while (leaf_idx != -1) {
            Node node;
            readNode(leaf_idx, node);

            for (int i = 0; i < node.count; i++) {
                int cmp = strcmp(node.entries[i].key, key_str);
                if (cmp == 0) {
                    result.push_back(node.entries[i].value);
                } else if (cmp > 0) {
                    // Past our key
                    std::sort(result.begin(), result.end());
                    return;
                }
            }

            leaf_idx = node.next;
        }

        std::sort(result.begin(), result.end());
    }

    void remove(const char* key_str, int value) {
        if (root_offset == -1) return;

        int leaf_idx = findLeafNode(key_str);
        if (leaf_idx == -1) return;

        Node node;
        readNode(leaf_idx, node);

        KeyValue target(key_str, value);
        int idx = -1;
        for (int i = 0; i < node.count; i++) {
            if (node.entries[i] == target) {
                idx = i;
                break;
            }
        }

        if (idx == -1) return;

        // Remove the entry
        for (int i = idx; i < node.count - 1; i++) {
            node.entries[i] = node.entries[i + 1];
        }
        node.count--;

        writeNode(leaf_idx, node);
    }
};

#endif
