#pragma once
#include "ToolScreen.h"

// One HTTP(S) request, curl -I / -v style: status, redirect, key headers,
// timing, and the start of the body. HTTPS is not verified (a diagnostic,
// not a secure client).
class HttpRequestScreen : public ToolScreen {
public:
  const char* title() override { return "HTTP Request"; }

protected:
  void _buildRows() override;
  void _editRow(uint8_t id) override;
  bool _begin() override;
  bool _step() override;

private:
  enum Row : uint8_t { ROW_URL, ROW_METHOD, ROW_REDIRECTS };
  String _url;
  bool   _head = false;       // GET or HEAD
  bool   _follow = false;     // follow redirects
};
