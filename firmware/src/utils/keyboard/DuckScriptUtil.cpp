#include "DuckScriptUtil.h"
#include <ctype.h>
#include <string.h>

// ── Utility ────────────────────────────────────────────────────────────────

bool DuckScriptUtil::_isIdentStart(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool DuckScriptUtil::_isIdentChar(char c)
{
  return _isIdentStart(c) || (c >= '0' && c <= '9');
}

String DuckScriptUtil::_trim(const String& s)
{
  String r = s;
  r.trim();
  return r;
}

bool DuckScriptUtil::_isModifierWord(const String& tok, uint8_t& out)
{
  if (tok == "CTRL" || tok == "CONTROL")                       { out = KEY_LEFT_CTRL;  return true; }
  if (tok == "SHIFT")                                          { out = KEY_LEFT_SHIFT; return true; }
  if (tok == "ALT"  || tok == "OPTION")                        { out = KEY_LEFT_ALT;   return true; }
  if (tok == "GUI"  || tok == "WINDOWS" || tok == "COMMAND")   { out = KEY_LEFT_GUI;   return true; }
  return false;
}

void DuckScriptUtil::_tokenize(const String& line, std::vector<String>& out)
{
  out.clear();
  int start = 0;
  int len = (int)line.length();
  for (int i = 0; i <= len; i++) {
    if (i == len || line[i] == ' ' || line[i] == '\t') {
      if (i > start) out.push_back(line.substring(start, i));
      start = i + 1;
    }
  }
}

// ── Key name → HID code ────────────────────────────────────────────────────

uint8_t DuckScriptUtil::_charToHID(const char* str)
{
  uint8_t value = 0;
  size_t  len   = strlen(str);
  if (len == 1) {
    value = (uint8_t)str[0];
  } else if (len >= 2 && len <= 3 && (str[0] == 'F' || str[0] == 'f') && isdigit((unsigned char)str[1])) {
    int fn = atoi(&str[1]);
    if (fn >= 1 && fn <= 12) value = (uint8_t)(0xC1 + fn);
  } else if (strcmp(str, "ENTER") == 0 || strcmp(str, "RETURN") == 0) {
    value = KEY_RETURN;
  } else if (strcmp(str, "ESC") == 0 || strcmp(str, "ESCAPE") == 0) {
    value = KEY_ESC;
  } else if (strcmp(str, "SPACE") == 0) {
    value = (uint8_t)' ';
  } else if (strcmp(str, "TAB") == 0) {
    value = KEY_TAB;
  } else if (strcmp(str, "BACKSPACE") == 0) {
    value = KEY_BACKSPACE;
  } else if (strcmp(str, "DELETE") == 0 || strcmp(str, "DEL") == 0) {
    value = KEY_DELETE;
  } else if (strcmp(str, "INSERT") == 0) {
    value = KEY_INSERT;
  } else if (strcmp(str, "CAPSLOCK") == 0) {
    value = KEY_CAPS_LOCK;
  } else if (strcmp(str, "UP") == 0 || strcmp(str, "UPARROW") == 0) {
    value = KEY_UP_ARROW;
  } else if (strcmp(str, "DOWN") == 0 || strcmp(str, "DOWNARROW") == 0) {
    value = KEY_DOWN_ARROW;
  } else if (strcmp(str, "LEFT") == 0 || strcmp(str, "LEFTARROW") == 0) {
    value = KEY_LEFT_ARROW;
  } else if (strcmp(str, "RIGHT") == 0 || strcmp(str, "RIGHTARROW") == 0) {
    value = KEY_RIGHT_ARROW;
  } else if (strcmp(str, "HOME") == 0) {
    value = KEY_HOME;
  } else if (strcmp(str, "END") == 0) {
    value = KEY_END;
  } else if (strcmp(str, "PAGEUP") == 0) {
    value = KEY_PAGE_UP;
  } else if (strcmp(str, "PAGEDOWN") == 0) {
    value = KEY_PAGE_DOWN;
  }
  return value;
}

void DuckScriptUtil::_holdPress(uint8_t modifier, uint8_t key)
{
  KeyReport r = {};
  _keyboard->reportModifier(&r, modifier);
  _keyboard->reportModifier(&r, key);
  _keyboard->sendReport(&r);
  delay(50);
  _keyboard->releaseAll();
}

void DuckScriptUtil::_holdPress2(uint8_t m1, uint8_t m2, uint8_t key)
{
  KeyReport r = {};
  _keyboard->reportModifier(&r, m1);
  _keyboard->reportModifier(&r, m2);
  _keyboard->reportModifier(&r, key);
  _keyboard->sendReport(&r);
  delay(50);
  _keyboard->releaseAll();
}

void DuckScriptUtil::_holdPress3(uint8_t m1, uint8_t m2, uint8_t m3, uint8_t key)
{
  KeyReport r = {};
  _keyboard->reportModifier(&r, m1);
  _keyboard->reportModifier(&r, m2);
  _keyboard->reportModifier(&r, m3);
  _keyboard->reportModifier(&r, key);
  _keyboard->sendReport(&r);
  delay(50);
  _keyboard->releaseAll();
}

// ── Expression parser ──────────────────────────────────────────────────────
//
// Recursive descent, C-style precedence (low → high):
//   ||  &&  |  ^  &  ==/!=  </>/<=/>=  <</>>  +/-  *///%  unary -/!  primary
// Primary: integer literal (decimal / 0xHEX / 0bBIN), $var, #const, (expr),
//          TRUE / FALSE.

namespace {

struct Parser {
  const char* p;
  const char* end;
  bool err = false;
  const std::map<String, int32_t>* vars;
  const std::map<String, int32_t>* defines;

  void skip() { while (p < end && (*p == ' ' || *p == '\t')) p++; }

  bool consume2(char a, char b) {
    if (p + 1 < end && p[0] == a && p[1] == b) { p += 2; return true; }
    return false;
  }

  int32_t parseExpr() { return parseOr(); }

  int32_t parseOr() {
    int32_t v = parseAnd();
    while (!err) {
      skip();
      if (consume2('|', '|')) { int32_t r = parseAnd(); v = (v || r) ? 1 : 0; }
      else break;
    }
    return v;
  }

  int32_t parseAnd() {
    int32_t v = parseBitOr();
    while (!err) {
      skip();
      if (consume2('&', '&')) { int32_t r = parseBitOr(); v = (v && r) ? 1 : 0; }
      else break;
    }
    return v;
  }

  int32_t parseBitOr() {
    int32_t v = parseBitXor();
    while (!err) {
      skip();
      if (p < end && *p == '|' && !(p + 1 < end && (p[1] == '|' || p[1] == '='))) {
        p++; int32_t r = parseBitXor(); v = v | r;
      } else break;
    }
    return v;
  }

  int32_t parseBitXor() {
    int32_t v = parseBitAnd();
    while (!err) {
      skip();
      if (p < end && *p == '^') { p++; int32_t r = parseBitAnd(); v = v ^ r; }
      else break;
    }
    return v;
  }

  int32_t parseBitAnd() {
    int32_t v = parseEq();
    while (!err) {
      skip();
      if (p < end && *p == '&' && !(p + 1 < end && p[1] == '&')) {
        p++; int32_t r = parseEq(); v = v & r;
      } else break;
    }
    return v;
  }

  int32_t parseEq() {
    int32_t v = parseRel();
    while (!err) {
      skip();
      if (consume2('=', '='))      { int32_t r = parseRel(); v = (v == r) ? 1 : 0; }
      else if (consume2('!', '=')) { int32_t r = parseRel(); v = (v != r) ? 1 : 0; }
      else break;
    }
    return v;
  }

  int32_t parseRel() {
    int32_t v = parseShift();
    while (!err) {
      skip();
      if (consume2('<', '='))      { int32_t r = parseShift(); v = (v <= r) ? 1 : 0; }
      else if (consume2('>', '=')) { int32_t r = parseShift(); v = (v >= r) ? 1 : 0; }
      else if (p < end && *p == '<' && !(p + 1 < end && p[1] == '<')) {
        p++; int32_t r = parseShift(); v = (v < r) ? 1 : 0;
      } else if (p < end && *p == '>' && !(p + 1 < end && p[1] == '>')) {
        p++; int32_t r = parseShift(); v = (v > r) ? 1 : 0;
      } else break;
    }
    return v;
  }

  int32_t parseShift() {
    int32_t v = parseAdd();
    while (!err) {
      skip();
      if (consume2('<', '<'))      { int32_t r = parseAdd(); v = v << r; }
      else if (consume2('>', '>')) { int32_t r = parseAdd(); v = v >> r; }
      else break;
    }
    return v;
  }

  int32_t parseAdd() {
    int32_t v = parseMul();
    while (!err) {
      skip();
      if (p < end && *p == '+') { p++; int32_t r = parseMul(); v += r; }
      else if (p < end && *p == '-') { p++; int32_t r = parseMul(); v -= r; }
      else break;
    }
    return v;
  }

  int32_t parseMul() {
    int32_t v = parseUnary();
    while (!err) {
      skip();
      if (p < end && *p == '*') { p++; int32_t r = parseUnary(); v *= r; }
      else if (p < end && *p == '/') { p++; int32_t r = parseUnary(); v = (r == 0) ? 0 : v / r; }
      else if (p < end && *p == '%') { p++; int32_t r = parseUnary(); v = (r == 0) ? 0 : v % r; }
      else break;
    }
    return v;
  }

  int32_t parseUnary() {
    skip();
    if (p < end && *p == '-') { p++; return -parseUnary(); }
    if (p < end && *p == '+') { p++; return parseUnary(); }
    if (p < end && *p == '!') { p++; return parseUnary() ? 0 : 1; }
    return parsePrimary();
  }

  int32_t parsePrimary() {
    skip();
    if (p >= end) { err = true; return 0; }

    if (*p == '(') {
      p++;
      int32_t v = parseExpr();
      skip();
      if (p >= end || *p != ')') { err = true; return 0; }
      p++;
      return v;
    }

    if (*p == '$' || *p == '#') {
      char kind = *p++;
      if (p >= end || !DuckScriptUtil::_isIdentStart(*p)) { err = true; return 0; }
      const char* s = p;
      while (p < end && DuckScriptUtil::_isIdentChar(*p)) p++;
      String name(s, (unsigned int)(p - s));
      const auto& m = (kind == '$') ? *vars : *defines;
      auto it = m.find(name);
      return (it == m.end()) ? 0 : it->second;
    }

    if (isdigit((unsigned char)*p)) {
      int base = 10;
      if (*p == '0' && p + 1 < end && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
      else if (*p == '0' && p + 1 < end && (p[1] == 'b' || p[1] == 'B')) { p += 2; base = 2; }
      int32_t v = 0;
      bool any = false;
      while (p < end) {
        int d = -1;
        char c = *p;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0 || d >= base) break;
        v = v * base + d;
        p++;
        any = true;
      }
      if (!any) { err = true; return 0; }
      return v;
    }

    auto matchKw = [&](const char* kw, size_t klen) -> bool {
      if ((size_t)(end - p) < klen) return false;
      if (strncasecmp(p, kw, klen) != 0) return false;
      if ((size_t)(end - p) > klen && DuckScriptUtil::_isIdentChar(p[klen])) return false;
      p += klen;
      return true;
    };
    if (matchKw("TRUE", 4))  return 1;
    if (matchKw("FALSE", 5)) return 0;

    err = true;
    return 0;
  }
};

} // namespace

bool DuckScriptUtil::_evalExpr(const String& expr, int32_t& out)
{
  String s = _trim(expr);
  if (s.length() == 0) return false;
  Parser P;
  P.p       = s.c_str();
  P.end     = P.p + s.length();
  P.vars    = &_vars;
  P.defines = &_defines;
  int32_t v = P.parseExpr();
  P.skip();
  if (P.err || P.p != P.end) return false;
  out = v;
  return true;
}

// ── STRING / STRINGLN interpolation ────────────────────────────────────────

bool DuckScriptUtil::_interpolate(const String& text, String& out)
{
  out = "";
  out.reserve(text.length() + 16);
  int len = (int)text.length();
  for (int i = 0; i < len; ) {
    char c = text[i];
    if ((c == '$' || c == '#') && i + 1 < len) {
      char nx = text[i + 1];
      if (nx == c) { out += c; i += 2; continue; }    // $$ → $, ## → #
      if (_isIdentStart(nx)) {
        int j = i + 1;
        while (j < len && _isIdentChar(text[j])) j++;
        String name = text.substring(i + 1, j);
        const auto& m = (c == '$') ? _vars : _defines;
        auto it = m.find(name);
        if (it != m.end()) out += String(it->second);
        else               out += text.substring(i, j);
        i = j;
        continue;
      }
    }
    out += c;
    i++;
  }
  return true;
}

// ── Modifier dispatch (space- and hyphen-separated) ────────────────────────

bool DuckScriptUtil::_handleSpaceModifiers(const String& line)
{
  std::vector<String> spaceToks;
  _tokenize(line, spaceToks);
  if (spaceToks.empty()) return false;

  std::vector<uint8_t> mods;
  String key;
  bool   keyDone = false;

  for (size_t k = 0; k < spaceToks.size(); k++) {
    if (keyDone) return false;

    const String& tok = spaceToks[k];

    std::vector<String> sub;
    int s = 0, tlen = (int)tok.length();
    for (int j = 0; j <= tlen; j++) {
      if (j == tlen || tok[j] == '-') {
        if (j > s) sub.push_back(tok.substring(s, j));
        s = j + 1;
      }
    }

    bool allMods = !sub.empty();
    std::vector<uint8_t> subMods;
    for (size_t i = 0; i < sub.size(); i++) {
      uint8_t m;
      if (!_isModifierWord(sub[i], m)) { allMods = false; break; }
      subMods.push_back(m);
    }

    if (allMods) {
      for (size_t i = 0; i < subMods.size(); i++) mods.push_back(subMods[i]);
    } else {
      key     = tok;
      keyDone = true;
    }
  }

  if (mods.empty()) return false;

  KeyReport r = {};
  for (size_t i = 0; i < mods.size(); i++) _keyboard->reportModifier(&r, mods[i]);
  if (key.length()) {
    uint8_t k = _charToHID(key.c_str());
    if (!k) return false;
    _keyboard->reportModifier(&r, k);
  }
  _keyboard->sendReport(&r);
  delay(50);
  _keyboard->releaseAll();
  return true;
}

// ── VAR / DEFINE / assignment ──────────────────────────────────────────────

bool DuckScriptUtil::_handleAssignment(const String& line)
{
  // VAR $name = expr     OR     $name = expr
  int start = 0;
  if (line.startsWith("VAR ")) start = 4;
  else if (line.length() > 0 && line[0] == '$') start = 0;
  else return false;

  int p = start;
  int len = (int)line.length();
  while (p < len && (line[p] == ' ' || line[p] == '\t')) p++;
  if (p >= len || line[p] != '$') return false;
  p++;
  int nameStart = p;
  if (p >= len || !_isIdentStart(line[p])) return false;
  while (p < len && _isIdentChar(line[p])) p++;
  String name = line.substring(nameStart, p);
  while (p < len && (line[p] == ' ' || line[p] == '\t')) p++;
  if (p >= len || line[p] != '=') return false;
  p++;
  // Reject == so comparison doesn't get misparsed as assignment.
  if (p < len && line[p] == '=') return false;
  String expr = line.substring(p);
  int32_t v;
  if (!_evalExpr(expr, v)) return false;
  _vars[name] = v;
  return true;
}

bool DuckScriptUtil::_handleDefine(const String& line)
{
  // DEFINE #name value      OR     DEFINE #name = value
  if (!line.startsWith("DEFINE ")) return false;
  int p = 7, len = (int)line.length();
  while (p < len && (line[p] == ' ' || line[p] == '\t')) p++;
  if (p >= len || line[p] != '#') return false;
  p++;
  int nameStart = p;
  if (p >= len || !_isIdentStart(line[p])) return false;
  while (p < len && _isIdentChar(line[p])) p++;
  String name = line.substring(nameStart, p);
  while (p < len && (line[p] == ' ' || line[p] == '\t')) p++;
  if (p < len && line[p] == '=') p++;
  String expr = line.substring(p);
  int32_t v;
  if (!_evalExpr(expr, v)) return false;
  _defines[name] = v;
  return true;
}

// ── Function call ──────────────────────────────────────────────────────────

bool DuckScriptUtil::_handleFnCall(const String& line)
{
  int len = (int)line.length();
  if (len < 3) return false;
  if (!_isIdentStart(line[0])) return false;
  int p = 1;
  while (p < len && _isIdentChar(line[p])) p++;
  String name = line.substring(0, p);
  while (p < len && (line[p] == ' ' || line[p] == '\t')) p++;
  if (p >= len || line[p] != '(') return false;
  p++;
  while (p < len && (line[p] == ' ' || line[p] == '\t')) p++;
  if (p >= len || line[p] != ')') return false;
  p++;
  while (p < len && (line[p] == ' ' || line[p] == '\t')) p++;
  if (p != len) return false;

  auto it = _functions.find(name);
  if (it == _functions.end()) return false;
  _callStack.push_back(_ip + 1);
  _ip = it->second;
  return true;
}

// ── Leaf command (no control flow) ─────────────────────────────────────────

bool DuckScriptUtil::_execLeaf(const String& line)
{
  if (line.length() == 0) return true;

  if (line == "REM" || line.startsWith("REM ") || line.startsWith("REM\t")) return true;

  if (line.startsWith("STRING ")) {
    String s = line.substring(7), out;
    _interpolate(s, out);
    _keyboard->write(reinterpret_cast<const uint8_t*>(out.c_str()), out.length());
    return true;
  }
  if (line == "STRING") return true;

  if (line.startsWith("STRINGLN ")) {
    String s = line.substring(9), out;
    _interpolate(s, out);
    _keyboard->write(reinterpret_cast<const uint8_t*>(out.c_str()), out.length());
    _keyboard->write(KEY_RETURN);
    return true;
  }
  if (line == "STRINGLN") {
    _keyboard->write(KEY_RETURN);
    return true;
  }

  if (line.startsWith("DELAY ")) {
    String expr = line.substring(6);
    int32_t v;
    if (!_evalExpr(expr, v)) v = (int32_t)expr.toInt();
    if (v < 0) v = 0;
    delay((uint32_t)v);
    return true;
  }

  if (line.startsWith("VAR ") || (line.length() && line[0] == '$')) {
    if (_handleAssignment(line)) return true;
  }
  if (line.startsWith("DEFINE ")) {
    if (_handleDefine(line)) return true;
  }

  uint8_t k = _charToHID(line.c_str());
  if (k) { _keyboard->write(k); return true; }

  if (_handleSpaceModifiers(line)) return true;

  return false;
}

// ── runCommand (1.0-compat single line) ────────────────────────────────────

bool DuckScriptUtil::runCommand(const String& line)
{
  return _execLeaf(line);
}

// ── Pre-scan ───────────────────────────────────────────────────────────────

bool DuckScriptUtil::_scanProgram()
{
  _ifSkip.clear();
  _elseSkip.clear();
  _whileEnd.clear();
  _whileBack.clear();
  _funcSkip.clear();
  _functions.clear();

  struct Frame {
    enum { F_IF, F_WHILE, F_FN } kind;
    int ip;
    int elseIp;
    String name;
  };
  std::vector<Frame> stack;
  bool inRemBlock = false;

  int n = (int)_lines.size();
  for (int i = 0; i < n; i++) {
    const String& raw = _lines[i];

    if (inRemBlock) {
      if (raw == "END_REM" || raw.startsWith("END_REM")) inRemBlock = false;
      continue;
    }
    if (raw == "REM_BLOCK" || raw.startsWith("REM_BLOCK")) {
      inRemBlock = true;
      continue;
    }
    if (raw.length() == 0) continue;

    if (raw.startsWith("IF ") && raw.endsWith(" THEN")) {
      Frame f; f.kind = Frame::F_IF; f.ip = i; f.elseIp = -1;
      stack.push_back(f);
    }
    else if (raw == "ELSE") {
      if (stack.empty() || stack.back().kind != Frame::F_IF) return false;
      stack.back().elseIp = i;
    }
    else if (raw == "END_IF") {
      if (stack.empty() || stack.back().kind != Frame::F_IF) return false;
      Frame f = stack.back(); stack.pop_back();
      if (f.elseIp >= 0) {
        _ifSkip[f.ip]       = f.elseIp;
        _elseSkip[f.elseIp] = i;
      } else {
        _ifSkip[f.ip]       = i;
      }
    }
    else if (raw.startsWith("WHILE ")) {
      Frame f; f.kind = Frame::F_WHILE; f.ip = i;
      stack.push_back(f);
    }
    else if (raw == "END_WHILE") {
      if (stack.empty() || stack.back().kind != Frame::F_WHILE) return false;
      Frame f = stack.back(); stack.pop_back();
      _whileEnd[f.ip] = i;
      _whileBack[i]   = f.ip;
    }
    else if (raw.startsWith("FUNCTION ")) {
      String rest = raw.substring(9);
      rest.trim();
      int paren = rest.indexOf('(');
      if (paren <= 0) return false;
      String name = rest.substring(0, paren);
      name.trim();
      Frame f; f.kind = Frame::F_FN; f.ip = i; f.name = name;
      stack.push_back(f);
      _functions[name] = i + 1;
    }
    else if (raw == "END_FUNCTION") {
      if (stack.empty() || stack.back().kind != Frame::F_FN) return false;
      Frame f = stack.back(); stack.pop_back();
      _funcSkip[f.ip] = i;
    }
  }

  return stack.empty();
}

// ── runScript (full interpreter) ───────────────────────────────────────────

void DuckScriptUtil::runScript(const String& content, LineCallback cb)
{
  _lines.clear();
  _vars.clear();
  _defines.clear();
  _functions.clear();
  _ifSkip.clear();
  _elseSkip.clear();
  _whileEnd.clear();
  _whileBack.clear();
  _funcSkip.clear();
  _callStack.clear();
  _ip   = 0;
  _stop = false;
  _cb   = cb;

  int start = 0, total = (int)content.length();
  while (start <= total) {
    int end = content.indexOf('\n', start);
    if (end < 0) end = total;
    String line = content.substring(start, end);
    line.trim();
    _lines.push_back(line);
    if (end == total) break;
    start = end + 1;
  }

  if (!_scanProgram()) {
    if (_cb) _cb(String("[scan failed — check IF/WHILE/FUNCTION balance]"), false);
    _cb = nullptr;
    return;
  }

  bool inRemBlock  = false;
  int  guardSteps  = 0;
  const int kMaxSteps = 200000;

  while (_ip < (int)_lines.size() && !_stop) {
    if (++guardSteps > kMaxSteps) {
      if (_cb) _cb(String("[execution step limit reached]"), false);
      break;
    }

    const String& line = _lines[_ip];

    if (inRemBlock) {
      if (line == "END_REM" || line.startsWith("END_REM")) inRemBlock = false;
      _ip++;
      continue;
    }
    if (line.length() == 0) { _ip++; continue; }
    if (line == "REM_BLOCK" || line.startsWith("REM_BLOCK")) {
      inRemBlock = true;
      _ip++;
      continue;
    }

    bool ok      = true;
    bool advance = true;

    if (line.startsWith("IF ") && line.endsWith(" THEN")) {
      int32_t v;
      String expr = line.substring(3, line.length() - 5);
      if (!_evalExpr(expr, v)) { ok = false; }
      else if (!v) {
        auto it = _ifSkip.find(_ip);
        if (it != _ifSkip.end()) { _ip = it->second + 1; advance = false; }
        else                     { ok = false; }
      }
    }
    else if (line == "ELSE") {
      auto it = _elseSkip.find(_ip);
      if (it != _elseSkip.end()) { _ip = it->second + 1; advance = false; }
    }
    else if (line == "END_IF") {
      // fall through, advance by 1
    }
    else if (line.startsWith("WHILE ")) {
      int32_t v;
      String expr = line.substring(6);
      if (!_evalExpr(expr, v)) { ok = false; }
      else if (!v) {
        auto it = _whileEnd.find(_ip);
        if (it != _whileEnd.end()) { _ip = it->second + 1; advance = false; }
        else                       { ok = false; }
      }
    }
    else if (line == "END_WHILE") {
      auto it = _whileBack.find(_ip);
      if (it != _whileBack.end()) { _ip = it->second; advance = false; }
    }
    else if (line.startsWith("FUNCTION ")) {
      auto it = _funcSkip.find(_ip);
      if (it != _funcSkip.end()) { _ip = it->second + 1; advance = false; }
    }
    else if (line == "END_FUNCTION" || line == "RETURN" || line.startsWith("RETURN ")) {
      if (!_callStack.empty()) {
        _ip = _callStack.back();
        _callStack.pop_back();
        advance = false;
      }
    }
    else if (line == "STOP_PAYLOAD") {
      _stop = true;
    }
    else if (line == "RESTART_PAYLOAD") {
      _ip = 0;
      _callStack.clear();
      advance = false;
    }
    else if (line == "RESET") {
      _keyboard->releaseAll();
    }
    else if (_handleFnCall(line)) {
      advance = false;
    }
    else {
      ok = _execLeaf(line);
    }

    bool keepGoing = true;
    if (_cb) keepGoing = _cb(line, ok);
    if (!keepGoing) break;

    if (advance) _ip++;
  }

  _cb = nullptr;
}
