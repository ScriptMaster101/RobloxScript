//=============================================================================
//  sqlite_minimal.h — Minimal SQLite file parser (read-only, no deps)
//  Parses just enough to extract rows from Chromium cookie databases.
//  Handles: header, B-tree leaf pages, varints, serialized records.
//  Does NOT handle: overflow pages, internal pages, WAL, journal, views.
//  For cookie DBs this is fine — they're small, simple tables.
//=============================================================================

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <variant>

namespace sqlite_minimal {

// A single cell value — can be null, int, float, text, or blob.
using CellValue = std::variant<
    std::monostate,     // NULL
    int64_t,            // integer
    double,             // float
    std::string,        // text
    std::vector<uint8_t> // blob
>;

// A row = ordered list of column values
using Row = std::vector<CellValue>;

// Column metadata from sqlite_master CREATE TABLE parsing
struct ColumnDef {
    std::string name;
    int         index;
};

struct TableInfo {
    std::string name;
    std::vector<ColumnDef> columns;
    int         rootPage;     // B-tree root page number
    std::string createSql;
};

// =========================================================================
//  Database — open, parse, query
// =========================================================================

class Database {
public:
    // Open from file. Returns true on success.
    bool open(const std::wstring& path);
    bool open(const std::vector<uint8_t>& data); // from memory buffer

    // Get table schema info. Returns nullptr if not found.
    const TableInfo* getTable(const std::string& name) const;

    // Read all rows from a table. Filters by host_key if non-empty.
    std::vector<Row> readTable(const std::string& tableName,
                                const std::string& hostFilter = "");

    // Convenience: get column value from a row by name
    static const CellValue* getColumn(const Row& row,
                                       const std::vector<ColumnDef>& cols,
                                       const std::string& colName);

    // Helpers to extract typed values
    static bool        isNull(const CellValue& v);
    static int64_t     asInt(const CellValue& v);
    static std::string asText(const CellValue& v);
    static const std::vector<uint8_t>& asBlob(const CellValue& v);

    uint32_t pageSize() const { return m_pageSize; }

private:
    // --- Varint encoding ---
    static uint64_t readVarint(const uint8_t*& p);
    static uint64_t readVarint(const uint8_t* p, int& consumed);

    // --- Page I/O ---
    const uint8_t* getPage(uint32_t pageNum) const;
    uint32_t       getPageOffset(uint32_t pageNum) const;
    uint32_t       getPageDataOffset(uint32_t pageNum) const;

    // --- B-tree leaf page parsing ---
    // Returns cell payloads (raw bytes) for a leaf table page.
    struct Cell {
        uint64_t           rowId;
        const uint8_t*     payload;
        uint32_t           payloadLen;
        uint32_t           overflowPage; // 0 if none
    };
    std::vector<Cell> readLeafCells(uint32_t pageNum);
    uint32_t          getNextLeafPage(uint32_t pageNum); // right sibling ptr

    // --- Record deserialization ---
    static Row deserializeRecord(const uint8_t* payload, uint32_t len);

    // --- Schema parsing ---
    void parseSchema();
    static std::vector<ColumnDef> parseCreateTable(const std::string& sql);

    // --- Data ---
    std::vector<uint8_t> m_data;
    uint32_t             m_pageSize = 0;
    uint32_t             m_numPages = 0;
    std::map<std::string, TableInfo> m_tables;
};

} // namespace sqlite_minimal
