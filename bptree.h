#ifndef BPTREE_H
#define BPTREE_H

#include <cstring>
#include <fstream>
#include <algorithm>
#include <vector>

const int MAX_KEY_LEN = 64;

struct Record {
    char key[MAX_KEY_LEN + 1];
    int value;

    Record() : value(0) {
        memset(key, 0, sizeof(key));
    }

    Record(const char* k, int v) : value(v) {
        memset(key, 0, sizeof(key));
        strncpy(key, k, MAX_KEY_LEN);
    }

    bool operator<(const Record& other) const {
        int c = strcmp(key, other.key);
        if (c != 0) return c < 0;
        return value < other.value;
    }

    bool operator==(const Record& other) const {
        return strcmp(key, other.key) == 0 && value == other.value;
    }

    bool keyEquals(const char* k) const {
        return strcmp(key, k) == 0;
    }
};

class BPTree {
private:
    std::fstream file;
    std::string filename;
    std::vector<Record> records;
    bool is_modified;

    void loadFromFile() {
        records.clear();
        std::ifstream infile(filename, std::ios::binary);
        if (!infile.is_open()) return;

        Record rec;
        while (infile.read(reinterpret_cast<char*>(&rec), sizeof(Record))) {
            records.push_back(rec);
        }
        infile.close();
    }

    void saveToFile() {
        if (!is_modified) return;

        std::ofstream outfile(filename, std::ios::binary | std::ios::trunc);
        for (const auto& rec : records) {
            outfile.write(reinterpret_cast<const char*>(&rec), sizeof(Record));
        }
        outfile.close();
        is_modified = false;
    }

public:
    BPTree(const std::string& fname) : filename(fname), is_modified(false) {
        loadFromFile();
    }

    ~BPTree() {
        saveToFile();
    }

    void insert(const char* key, int value) {
        Record rec(key, value);

        // Check if already exists
        for (const auto& r : records) {
            if (r == rec) return;
        }

        records.push_back(rec);
        std::sort(records.begin(), records.end());
        is_modified = true;
    }

    void find(const char* key, std::vector<int>& result) {
        result.clear();
        for (const auto& rec : records) {
            if (rec.keyEquals(key)) {
                result.push_back(rec.value);
            }
        }
        std::sort(result.begin(), result.end());
    }

    void remove(const char* key, int value) {
        Record target(key, value);
        auto it = std::find(records.begin(), records.end(), target);
        if (it != records.end()) {
            records.erase(it);
            is_modified = true;
        }
    }

    void flush() {
        saveToFile();
    }
};

#endif
