#include "datafile.h"
#include "raylib.h"
#include <cstdlib>
#include <cstring>
#include <cctype>

static const std::string EMPTY;

const std::string& DataRecord::operator[](size_t i) const { return i < tok.size() ? tok[i] : EMPTY; }
float DataRecord::F(size_t i, float def) const { return i < tok.size() ? (float)atof(tok[i].c_str()) : def; }
int   DataRecord::I(size_t i, int def) const { return i < tok.size() ? atoi(tok[i].c_str()) : def; }
bool  DataRecord::Is(const char* type) const {
    if (tok.empty()) return false;
    const std::string& a = tok[0];
    size_t n = strlen(type);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)type[i])) return false;
    return true;
}

std::vector<DataRecord> ParseDataText(const char* text) {
    std::vector<DataRecord> out;
    if (!text) return out;
    int line = 0;
    const char* p = text;
    while (*p) {
        line++;
        const char* e = p;
        while (*e && *e != '\n') e++;
        std::string s(p, e);
        size_t hash = s.find('#');
        if (hash != std::string::npos) s.resize(hash);
        DataRecord r; r.line = line;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && isspace((unsigned char)s[i])) i++;
            size_t j = i;
            while (j < s.size() && !isspace((unsigned char)s[j])) j++;
            if (j > i) r.tok.push_back(s.substr(i, j - i));
            i = j;
        }
        if (!r.tok.empty()) out.push_back(r);
        p = *e ? e + 1 : e;
    }
    return out;
}

std::vector<DataRecord> ReadDataFile(const char* path, const char* fallbackText) {
    if (FileExists(path)) {
        char* text = LoadFileText(path);
        if (text) {
            auto recs = ParseDataText(text);
            UnloadFileText(text);
            TraceLog(LOG_INFO, "DATA: '%s' -> %d records", path, (int)recs.size());
            return recs;
        }
    }
    TraceLog(LOG_WARNING, "DATA: '%s' not found, using built-in defaults", path);
    return ParseDataText(fallbackText);
}
