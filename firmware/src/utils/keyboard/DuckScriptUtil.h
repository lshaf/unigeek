#pragma once

#include "HIDKeyboardUtil.h"
#include <Arduino.h>
#include <functional>
#include <vector>
#include <map>

class DuckScriptUtil {
public:
  // Per-line UI hook. Return false to abort the script.
  // ok=false means the line errored (parse / unknown command / failed expr).
  using LineCallback = std::function<bool(const String& line, bool ok)>;

  explicit DuckScriptUtil(HIDKeyboardUtil* keyboard) : _keyboard(keyboard) {}

  // DuckyScript 1.0 style — one line at a time, no control flow / variables.
  bool runCommand(const String& line);

  // Full interpreter: supports VAR / DEFINE / IF / WHILE / FUNCTION / RETURN
  // on top of every command runCommand handles. Loads the whole script first.
  void runScript(const String& content, LineCallback cb = nullptr);

  static bool _isIdentStart(char c);
  static bool _isIdentChar(char c);

private:
  HIDKeyboardUtil* _keyboard;

  std::vector<String> _lines;

  std::map<String, int32_t> _vars;
  std::map<String, int32_t> _defines;
  std::map<String, int>     _functions;

  std::map<int, int> _ifSkip;
  std::map<int, int> _elseSkip;
  std::map<int, int> _whileEnd;
  std::map<int, int> _whileBack;
  std::map<int, int> _funcSkip;

  std::vector<int> _callStack;
  int  _ip   = 0;
  bool _stop = false;
  LineCallback _cb;

  uint8_t _charToHID(const char* str);
  void    _holdPress (uint8_t mod, uint8_t key);
  void    _holdPress2(uint8_t m1, uint8_t m2, uint8_t key);
  void    _holdPress3(uint8_t m1, uint8_t m2, uint8_t m3, uint8_t key);
  bool    _execLeaf(const String& line);

  bool _scanProgram();
  bool _handleSpaceModifiers(const String& line);
  bool _handleAssignment(const String& line);
  bool _handleDefine(const String& line);
  bool _handleFnCall(const String& line);

  bool _evalExpr(const String& expr, int32_t& out);
  bool _interpolate(const String& text, String& out);

  static String _trim(const String& s);
  static bool   _isModifierWord(const String& tok, uint8_t& out);
  static void   _tokenize(const String& line, std::vector<String>& out);
};
