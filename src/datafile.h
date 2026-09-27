// =====================================================================================
//  Tiny line-based config reader used by every data file in assets/data/.
//
//  Format: one record per line, whitespace-separated tokens, '#' starts a comment.
//      CLASS  Hatchback  4.1  1.5  ...
//      SPRITE Sports     audi.png  3
//  Each module decides what the first token (the record type) means, so new record
//  types can be added without touching the parser.
// =====================================================================================
#pragma once
#include <string>
#include <vector>

struct DataRecord {
    std::vector<std::string> tok;
    int line = 0;
    const std::string& operator[](size_t i) const;
    size_t size() const { return tok.size(); }
    float  F(size_t i, float def = 0.0f) const;
    int    I(size_t i, int def = 0) const;
    bool   Is(const char* type) const;          // case-insensitive compare of token 0
};

// Parses a file (or, if it does not exist, the given fallback text).
std::vector<DataRecord> ReadDataFile(const char* path, const char* fallbackText = nullptr);
std::vector<DataRecord> ParseDataText(const char* text);
