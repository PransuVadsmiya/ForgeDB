#include "minidb.hpp"
#include "sqlexpr.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace minidb {

std::string trim(std::string text) {
  auto first = find_if_not(text.begin(), text.end(),
                          [](unsigned char c) { return isspace(c); });
  auto last = find_if_not(text.rbegin(), text.rend(),
                         [](unsigned char c) { return isspace(c); }).base();
  if (first >= last) return "";
  return string(first, last);
}

std::vector<std::string> splitStatements(const std::string& input) {
  vector<string> statements;
  string current;
  bool inQuote = false;
  bool inLineComment = false;
  for (size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if (!inQuote && c == '-' && i + 1 < input.size() && input[i + 1] == '-') {
      inLineComment = true;
    }
    if (inLineComment) {
      if (c == '\n') inLineComment = false;
      continue;
    }
    if (c == '\'' && (i == 0 || input[i - 1] != '\\')) {
      inQuote = !inQuote;
    }
    if (c == ';' && !inQuote) {
      statements.push_back(trim(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  const auto tail = trim(current);
  if (!tail.empty()) statements.push_back(tail);
  return statements;
}

void Index::clear() {
  btree.clear();
  hash.clear();
}

void Index::add(const string& key, size_t rowId) {
  if (kind == IndexKind::BTree) {
    btree[key].insert(rowId);
  } else {
    hash[key].insert(rowId);
  }
}

void Index::remove(const string& key, size_t rowId) {
  auto removeFrom = [rowId](auto& container, const string& k) {
    auto it = container.find(k);
    if (it == container.end()) return;
    it->second.erase(rowId);
    if (it->second.empty()) container.erase(it);
  };
  if (kind == IndexKind::BTree) {
    removeFrom(btree, key);
  } else {
    removeFrom(hash, key);
  }
}

set<size_t> Index::equality(const string& key) const {
  if (kind == IndexKind::BTree) {
    const auto it = btree.find(key);
    return it == btree.end() ? set<size_t>{} : it->second;
  }
  const auto it = hash.find(key);
  return it == hash.end() ? set<size_t>{} : it->second;
}

set<size_t> Index::range(const string& op, const string& key) const {
  set<size_t> result;
  if (kind != IndexKind::BTree) return result;
  
  auto append = [&result](const auto& item) {
    result.insert(item.second.begin(), item.second.end());
  };
  
  if (op == "<" || op == "<=") {
    for (auto it = btree.begin(); it != btree.end(); ++it) {
      if ((op == "<" && it->first < key) || (op == "<=" && it->first <= key)) {
        append(*it);
      } else {
        break;
      }
    }
  } else if (op == ">" || op == ">=") {
    auto it = op == ">" ? btree.upper_bound(key) : btree.lower_bound(key);
    for (; it != btree.end(); ++it) append(*it);
  }
  return result;
}

// Helper functions for parsing and utilities
string lower(string text) {
  transform(text.begin(), text.end(), text.begin(),
           [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return text;
}

string upper(string text) {
  transform(text.begin(), text.end(), text.begin(),
           [](unsigned char c) { return static_cast<char>(toupper(c)); });
  return text;
}

bool startsWithKeyword(const string& sql, const string& keyword) {
  const auto clean = trim(sql);
  return clean.size() >= keyword.size() && upper(clean.substr(0, keyword.size())) == keyword;
}

vector<string> splitComma(const string& text) {
  vector<string> parts;
  string current;
  bool inQuote = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\'' && (i == 0 || text[i - 1] != '\\')) inQuote = !inQuote;
    if (c == ',' && !inQuote) {
      parts.push_back(trim(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty() || !parts.empty()) parts.push_back(trim(current));
  return parts;
}

vector<string> splitAnd(const string& text) {
  vector<string> parts;
  string current;
  bool inQuote = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\'' && (i == 0 || text[i - 1] != '\\')) inQuote = !inQuote;
    if (!inQuote && i + 5 <= text.size() && upper(text.substr(i, 5)) == " AND ") {
      parts.push_back(trim(current));
      current.clear();
      i += 4;
    } else {
      current.push_back(c);
    }
  }
  if (!trim(current).empty()) parts.push_back(trim(current));
  return parts;
}

size_t findKeyword(const string& text, const string& keyword) {
  const auto needle = upper(keyword);
  const auto haystack = upper(text);
  bool inQuote = false;
  for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
    const char c = text[i];
    if (c == '\'' && (i == 0 || text[i - 1] != '\\')) inQuote = !inQuote;
    const bool beforeBoundary = i == 0 || isspace(static_cast<unsigned char>(text[i - 1]));
    const bool afterBoundary = i + needle.size() == text.size() ||
                               isspace(static_cast<unsigned char>(text[i + needle.size()]));
    if (!inQuote && beforeBoundary && afterBoundary && haystack.substr(i, needle.size()) == needle) {
      return i;
    }
  }
  return string::npos;
}

string unquote(string value) {
  value = trim(value);
  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
    string result;
    for (size_t i = 1; i + 1 < value.size(); ++i) {
      if (value[i] == '\\' && i + 2 < value.size()) {
        result.push_back(value[++i]);
      } else {
        result.push_back(value[i]);
      }
    }
    return result;
  }
  return value;
}

bool isInteger(const string& value) {
  if (value.empty()) return false;
  const size_t start = (value[0] == '-' || value[0] == '+') ? 1 : 0;
  if (start == value.size()) return false;
  return all_of(value.begin() + static_cast<ptrdiff_t>(start), value.end(),
               [](unsigned char c) { return isdigit(c); });
}

FieldType parseType(const string& text) {
  const auto type = upper(trim(text));
  if (type == "INT" || type == "INTEGER") return FieldType::Int;
  if (type == "TEXT" || type == "VARCHAR") return FieldType::Text;
  throw runtime_error("unsupported column type: " + text);
}

string typeName(FieldType type) {
  return type == FieldType::Int ? "INT" : "TEXT";
}

IndexKind parseIndexKind(const string& text) {
  const auto kind = upper(trim(text));
  if (kind.empty() || kind == "BTREE") return IndexKind::BTree;
  if (kind == "HASH") return IndexKind::Hash;
  throw runtime_error("unsupported index type: " + text);
}

string indexKindName(IndexKind kind) {
  return kind == IndexKind::BTree ? "BTREE" : "HASH";
}

string encode(const string& value) {
  string out;
  for (const char c : value) {
    if (c == '\\' || c == '\t' || c == '\n' || c == '\r') {
      out.push_back('\\');
      if (c == '\t') out.push_back('t');
      else if (c == '\n') out.push_back('n');
      else if (c == '\r') out.push_back('r');
      else out.push_back('\\');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

string decode(const string& value) {
  string out;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      const char next = value[++i];
      if (next == 't') out.push_back('\t');
      else if (next == 'n') out.push_back('\n');
      else if (next == 'r') out.push_back('\r');
      else out.push_back(next);
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

vector<string> splitTsv(const string& line) {
  vector<string> parts;
  string current;
  bool escaped = false;
  for (const char c : line) {
    if (escaped) {
      current.push_back('\\');
      current.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '\t') {
      parts.push_back(decode(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (escaped) current.push_back('\\');
  parts.push_back(decode(current));
  return parts;
}

vector<Predicate> parsePredicates(const string& text) {
  vector<Predicate> predicates;
  const auto clean = trim(text);
  if (clean.empty()) return predicates;
  
  static const regex predicateRegex(
      R"(^([A-Za-z_][A-Za-z0-9_]*)\s*(=|!=|<=|>=|<|>)\s*(.+)$)",
      regex::icase);
  for (const auto& part : splitAnd(clean)) {
    smatch match;
    if (!regex_match(part, match, predicateRegex)) {
      throw runtime_error("WHERE supports predicates joined by AND");
    }
    predicates.push_back(Predicate{lower(match[1].str()), match[2].str(), unquote(match[3].str())});
  }
  return predicates;
}

optional<Aggregate> parseAggregate(const vector<string>& projection) {
  if (projection.size() != 1) return nullopt;
  
  static const regex aggregateRegex(
      R"(^(COUNT|MIN|MAX|AVG)\s*\(\s*(\*|[A-Za-z_][A-Za-z0-9_]*)\s*\)$)",
      regex::icase);
  smatch match;
  const auto expression = trim(projection[0]);
  if (!regex_match(expression, match, aggregateRegex)) return nullopt;
  
  return Aggregate{upper(match[1].str()), lower(match[2].str())};
}

string formatRows(const vector<string>& headers,
                 const vector<vector<string>>& rows,
                 const string& plan) {
  ostringstream out;
  out << plan << "\n";
  for (size_t i = 0; i < headers.size(); ++i) {
    if (i) out << '\t';
    out << headers[i];
  }
  out << "\n";
  for (size_t i = 0; i < headers.size(); ++i) {
    if (i) out << '\t';
    out << "----";
  }
  out << "\n";
  for (const auto& row : rows) {
    for (size_t i = 0; i < row.size(); ++i) {
      if (i) out << '\t';
      out << row[i];
    }
    out << "\n";
  }
  out << rows.size() << " row(s)";
  return out.str();
}

SelectQuery parseSelectQuery(const string& sql, bool explainOnly) {
  string clean = trim(sql);
  if (startsWithKeyword(clean, "EXPLAIN")) {
    clean = trim(clean.substr(7));
    if (startsWithKeyword(clean, "ANALYZE")) clean = trim(clean.substr(7));
    explainOnly = true;
  }
  if (!startsWithKeyword(clean, "SELECT")) {
    throw runtime_error("syntax: SELECT columns FROM table [JOIN ...] [WHERE ...] [ORDER BY ...] [LIMIT n]");
  }
  clean = trim(clean.substr(6));
  const auto fromPos = findKeyword(clean, "FROM");
  if (fromPos == string::npos) {
    throw runtime_error("SELECT requires FROM");
  }

  SelectQuery query;
  query.explainOnly = explainOnly;
  query.projection = splitComma(clean.substr(0, fromPos));
  for (auto& item : query.projection) item = trim(item);
  query.aggregate = parseAggregate(query.projection);

  string rest = trim(clean.substr(fromPos + 4));
  
  // Find clause positions
  const auto innerJoinPos = findKeyword(rest, "INNER JOIN");
  const auto leftJoinPos = findKeyword(rest, "LEFT JOIN");
  const auto leftOuterJoinPos = findKeyword(rest, "LEFT OUTER JOIN");
  const auto rightJoinPos = findKeyword(rest, "RIGHT JOIN");
  const auto rightOuterJoinPos = findKeyword(rest, "RIGHT OUTER JOIN");
  const auto fullJoinPos = findKeyword(rest, "FULL JOIN");
  const auto fullOuterJoinPos = findKeyword(rest, "FULL OUTER JOIN");
  const auto crossJoinPos = findKeyword(rest, "CROSS JOIN");
  const auto joinPos = findKeyword(rest, "JOIN");
  const auto wherePos = findKeyword(rest, "WHERE");
  const auto orderPos = findKeyword(rest, "ORDER BY");
  const auto limitPos = findKeyword(rest, "LIMIT");
  
  // Determine the first join position (if any)
  size_t firstJoinPos = string::npos;
  JoinType firstJoinType = JoinType::Inner;
  size_t joinKeywordLength = 0;
  
  if (leftOuterJoinPos != string::npos) {
    firstJoinPos = leftOuterJoinPos;
    firstJoinType = JoinType::Left;
    joinKeywordLength = 15; // "LEFT OUTER JOIN"
  } else if (rightOuterJoinPos != string::npos) {
    firstJoinPos = rightOuterJoinPos;
    firstJoinType = JoinType::Right;
    joinKeywordLength = 16; // "RIGHT OUTER JOIN"
  } else if (fullOuterJoinPos != string::npos) {
    firstJoinPos = fullOuterJoinPos;
    firstJoinType = JoinType::Full;
    joinKeywordLength = 15; // "FULL OUTER JOIN"
  } else if (innerJoinPos != string::npos) {
    firstJoinPos = innerJoinPos;
    firstJoinType = JoinType::Inner;
    joinKeywordLength = 10; // "INNER JOIN"
  } else if (leftJoinPos != string::npos) {
    firstJoinPos = leftJoinPos;
    firstJoinType = JoinType::Left;
    joinKeywordLength = 9; // "LEFT JOIN"
  } else if (rightJoinPos != string::npos) {
    firstJoinPos = rightJoinPos;
    firstJoinType = JoinType::Right;
    joinKeywordLength = 10; // "RIGHT JOIN"
  } else if (fullJoinPos != string::npos) {
    firstJoinPos = fullJoinPos;
    firstJoinType = JoinType::Full;
    joinKeywordLength = 9; // "FULL JOIN"
  } else if (crossJoinPos != string::npos) {
    firstJoinPos = crossJoinPos;
    firstJoinType = JoinType::Cross;
    joinKeywordLength = 10; // "CROSS JOIN"
  } else if (joinPos != string::npos) {
    firstJoinPos = joinPos;
    firstJoinType = JoinType::Inner;
    joinKeywordLength = 4; // "JOIN"
  }
  
  auto clauseEnd = [&](size_t start) {
    size_t end = rest.size();
    for (const auto pos : {wherePos, orderPos, limitPos}) {
      if (pos != string::npos && pos > start) {
        end = min(end, pos);
      }
    }
    return end;
  };

  const auto firstClause = min({firstJoinPos == string::npos ? rest.size() : firstJoinPos,
                                wherePos == string::npos ? rest.size() : wherePos,
                                orderPos == string::npos ? rest.size() : orderPos,
                                limitPos == string::npos ? rest.size() : limitPos});
  
  const string baseTableName = trim(rest.substr(0, firstClause));
  if (baseTableName.empty()) {
    throw runtime_error("SELECT requires a table name");
  }
  query.projection.push_back("__table__" + lower(baseTableName));
  
  // Parse JOIN clause if present
  if (firstJoinPos != string::npos) {
    string joinRest = trim(rest.substr(firstJoinPos + joinKeywordLength));
    
    // Find ON clause
    const auto onPos = findKeyword(joinRest, "ON");
    if (onPos == string::npos && firstJoinType != JoinType::Cross) {
      throw runtime_error("JOIN requires ON clause (except for CROSS JOIN)");
    }
    
    JoinClause join;
    join.type = firstJoinType;
    
    if (firstJoinType == JoinType::Cross) {
      // CROSS JOIN doesn't need ON clause
      const auto endPos = min({findKeyword(joinRest, "WHERE") == string::npos ? joinRest.size() : findKeyword(joinRest, "WHERE"),
                               findKeyword(joinRest, "ORDER BY") == string::npos ? joinRest.size() : findKeyword(joinRest, "ORDER BY"),
                               findKeyword(joinRest, "LIMIT") == string::npos ? joinRest.size() : findKeyword(joinRest, "LIMIT")});
      join.rightTable = lower(trim(joinRest.substr(0, endPos)));
      join.leftColumn = "";
      join.rightColumn = "";
    } else {
      // Get right table name (between JOIN and ON)
      join.rightTable = lower(trim(joinRest.substr(0, onPos)));
      
      // Parse ON condition
      const auto afterOn = trim(joinRest.substr(onPos + 2));
      const auto onEndPos = min({findKeyword(afterOn, "WHERE") == string::npos ? afterOn.size() : findKeyword(afterOn, "WHERE"),
                                 findKeyword(afterOn, "ORDER BY") == string::npos ? afterOn.size() : findKeyword(afterOn, "ORDER BY"),
                                 findKeyword(afterOn, "LIMIT") == string::npos ? afterOn.size() : findKeyword(afterOn, "LIMIT")});
      
      const string onCondition = trim(afterOn.substr(0, onEndPos));
      
      static const regex onRegex(
          R"(^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$)",
          regex::icase);
      smatch match;
      if (!regex_match(onCondition, match, onRegex)) {
        throw runtime_error("JOIN ON requires format: table1.column1 = table2.column2");
      }
      
      const string leftTableInOn = lower(match[1].str());
      const string leftColInOn = lower(match[2].str());
      const string rightTableInOn = lower(match[3].str());
      const string rightColInOn = lower(match[4].str());
      
      // Determine which is left and which is right based on base table
      if (leftTableInOn == lower(baseTableName)) {
        join.leftColumn = leftColInOn;
        join.rightColumn = rightColInOn;
      } else if (rightTableInOn == lower(baseTableName)) {
        join.leftColumn = rightColInOn;
        join.rightColumn = leftColInOn;
      } else {
        throw runtime_error("JOIN ON must reference the base table");
      }
    }
    
    query.joins.push_back(join);
  }

  if (wherePos != string::npos) {
    const auto end = clauseEnd(wherePos);
    const auto whereText = rest.substr(wherePos + 5, end - wherePos - 5);
    if (query.joins.empty()) {
      // Single-table: parse the full expression (OR / NOT / arithmetic /
      // functions) and extract the sargable AND-chain so the planner can still
      // use an index for the simple conjuncts.
      query.whereExpr = parseExpression(whereText);
      query.predicates = extractSargablePredicates(query.whereExpr);
    } else {
      // Join queries still use the simple AND-only predicate path.
      query.predicates = parsePredicates(whereText);
    }
  }

  if (orderPos != string::npos) {
    const auto end = clauseEnd(orderPos);
    stringstream ss(trim(rest.substr(orderPos + 8, end - orderPos - 8)));
    string column, direction;
    ss >> column >> direction;
    if (column.empty()) throw runtime_error("ORDER BY requires a column");
    query.orderBy = SortSpec{lower(column), upper(direction) == "DESC" ? SortDirection::Desc : SortDirection::Asc};
  }

  if (limitPos != string::npos) {
    const auto value = trim(rest.substr(limitPos + 5));
    if (!isInteger(value) || stoll(value) < 0) {
      throw runtime_error("LIMIT requires a non-negative integer");
    }
    query.limit = static_cast<size_t>(stoull(value));
  }
  return query;
}

string joinTypeName(JoinType type) {
  switch (type) {
    case JoinType::Inner: return "inner";
    case JoinType::Left: return "left";
    case JoinType::Right: return "right";
    case JoinType::Full: return "full outer";
    case JoinType::Cross: return "cross";
  }
  return "unknown";
}

}
