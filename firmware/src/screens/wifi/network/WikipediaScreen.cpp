//
// Created by L Shaf on 2026-06-08.
//

#include "WikipediaScreen.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/utility/FileViewerScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowQRCodeAction.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace {
// Latin-script Wikipedia editions only — the TFT renders ASCII, and accented
// Latin letters are transliterated to a base letter (see translitCp). Non-Latin
// scripts (cyrillic, CJK, arabic, thai, ...) can't be printed, so they're omitted.
struct WikiLang { const char* code; const char* name; };
constexpr WikiLang kWikiLangs[] = {
  { "en", "English" },    { "id", "Indonesia" },  { "ms", "Malay" },
  { "es", "Spanish" },    { "pt", "Portuguese" }, { "fr", "French" },
  { "de", "German" },     { "it", "Italian" },    { "nl", "Dutch" },
  { "ca", "Catalan" },    { "pl", "Polish" },     { "cs", "Czech" },
  { "sk", "Slovak" },     { "hr", "Croatian" },   { "hu", "Hungarian" },
  { "ro", "Romanian" },   { "tr", "Turkish" },    { "sw", "Swahili" },
  { "tl", "Filipino" },   { "eo", "Esperanto" },  { "la", "Latin" },
};
constexpr uint8_t kWikiLangCount = sizeof(kWikiLangs) / sizeof(kWikiLangs[0]);

// Map a Unicode codepoint to an ASCII transliteration, or nullptr if it has no
// reasonable Latin base (caller substitutes '?'). Covers Latin-1 Supplement,
// Latin Extended-A, and the Romanian comma-below letters in Latin Extended-B.
const char* translitCp(uint32_t cp) {
  if (cp < 0x80) return nullptr;

  if (cp >= 0xC0 && cp <= 0xFF) {
    static const char* const t[] = {
      "A","A","A","A","A","A","AE","C", "E","E","E","E","I","I","I","I",
      "D","N","O","O","O","O","O","x",  "O","U","U","U","U","Y","Th","ss",
      "a","a","a","a","a","a","ae","c", "e","e","e","e","i","i","i","i",
      "d","n","o","o","o","o","o","/",  "o","u","u","u","u","y","th","y",
    };
    return t[cp - 0xC0];
  }

  if (cp >= 0x100 && cp <= 0x17F) {
    static const char* const t[] = {
      "A","a","A","a","A","a","C","c", "C","c","C","c","C","c","D","d",
      "D","d","E","e","E","e","E","e", "E","e","E","e","G","g","G","g",
      "G","g","G","g","H","h","H","h", "I","i","I","i","I","i","I","i",
      "I","i","IJ","ij","J","j","K","k", "k","L","l","L","l","L","l","L",
      "l","L","l","N","n","N","n","N", "n","n","N","n","O","o","O","o",
      "O","o","OE","oe","R","r","R","r", "R","r","S","s","S","s","S","s",
      "S","s","T","t","T","t","T","t", "U","u","U","u","U","u","U","u",
      "U","u","U","u","W","w","Y","y", "Y","Z","z","Z","z","Z","z","s",
    };
    return t[cp - 0x100];
  }

  switch (cp) {
    case 0x218: return "S"; case 0x219: return "s";   // Ș ș (Romanian)
    case 0x21A: return "T"; case 0x21B: return "t";   // Ț ț (Romanian)
  }
  return nullptr;
}

// CSV membership / count for the polyglot achievement tracker.
bool csvHas(const String& csv, const String& item) {
  int pos = 0;
  while (pos < (int)csv.length()) {
    int c = csv.indexOf(',', pos);
    if (c < 0) c = csv.length();
    if (csv.substring(pos, c) == item) return true;
    pos = c + 1;
  }
  return false;
}
int csvCount(const String& csv) {
  if (csv.length() == 0) return 0;
  int n = 1;
  for (size_t i = 0; i < csv.length(); i++) if (csv[i] == ',') n++;
  return n;
}
} // namespace

void WikipediaScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage available");
    Screen.goBack();
    return;
  }
  _favLoad();
  _showMenu();
}

void WikipediaScreen::onUpdate() {
  // Drain ghost nav events queued by a blocking HTTP fetch / text input.
  if (_navReadyAt && millis() < _navReadyAt) {
    if (Uni.Nav->wasPressed()) Uni.Nav->readDirection();
    return;
  }
  _navReadyAt = 0;

  // Long-press a data row in a browseable list -> per-item context menu.
  if ((_state == STATE_RESULTS || _state == STATE_FILELIST) && !_holdFired &&
      Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 800) {
    uint8_t sel = _selectedIndex;
    if ((int8_t)sel != _rowPrev && (int8_t)sel != _rowNext) {
      _holdFired = true;
      _openItemMenu(sel);
      return;
    }
  }
  if (_holdFired) {
    if (Uni.Nav->wasPressed()) { Uni.Nav->readDirection(); _holdFired = false; }
    return;
  }

  ListScreen::onUpdate();
}

void WikipediaScreen::onBack() {
  if (_state == STATE_ITEM_MENU) { _returnFromContext(); return; }
  if (_state == STATE_RESULTS || _state == STATE_FILELIST || _state == STATE_ONTHISDAY) {
    _showMenu();
    return;
  }
  Screen.goBack();
}

void WikipediaScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    _menuIndex = index;
    if (_readOnly) {
      switch (index) {
        case 0: _showFavorites();  break;
        case 1: _showAllCached();  break;
        case 2: _searchCached();   break;
        case 3: _chooseLanguage(); break;
      }
      return;
    }
    switch (index) {
      case 0: _doSearch();       break;
      case 1: _showRandom();     break;
      case 2: _showOnThisDay();  break;
      case 3: _searchCached();   break;
      case 4: _showAllCached();  break;
      case 5: _showFavorites();  break;
      case 6: _chooseLanguage(); break;
    }
    return;
  }

  if (_state == STATE_RESULTS) {
    if (index == _rowPrev) { _runSearch(_srOffset - kPageSize); return; }
    if (index == _rowNext) { _runSearch(_srOffset + kPageSize); return; }
    _openResultByIdx(index - _rowDataStart);
    return;
  }

  if (_state == STATE_FILELIST) {
    if (index == _rowPrev) { _showFilePage(_flPage - 1); return; }
    if (index == _rowNext) { _showFilePage(_flPage + 1); return; }
    _openFileAbs((uint16_t)_flPage * kPageSize + (index - _rowDataStart));
    return;
  }

  if (_state == STATE_ONTHISDAY) {
    _openOtd(index);
    return;
  }

  if (_state == STATE_ITEM_MENU) {
    if (index >= _ctxCount) return;
    switch (_ctxActions[index]) {
      case CTX_OPEN:
        _returnFromContext();   // restore the list underneath the pushed viewer
        if (_ctxFrom == STATE_RESULTS) _openResultByIdx(_ctxResultIdx);
        else                           _viewArticle(_ctxPath, _ctxLang);
        break;
      case CTX_FAV: {
        bool now = _favToggle(_ctxRel);
        if (now) { int n = Achievement.inc("wiki_favorite"); if (n == 1) Achievement.unlock("wiki_favorite"); }
        _returnFromContext();
        break;
      }
      case CTX_SHARE:
        _shareQr(_ctxTitleForUrl, _ctxLang, _ctxLabel);
        _returnFromContext();
        break;
      case CTX_CANCEL:
        _returnFromContext();
        break;
    }
    return;
  }
}

// ── paged-list helper ──────────────────────────────────

void WikipediaScreen::_composePage(ListItem* data, uint8_t dataCount,
                                   bool hasPrev, bool hasNext) {
  uint8_t n = 0;
  _rowPrev = _rowNext = -1;

  if (hasPrev) { _listItems[n] = { "< Prev page", nullptr }; _rowPrev = n; n++; }

  _rowDataStart = n;
  for (uint8_t i = 0; i < dataCount; i++) _listItems[n++] = data[i];

  if (hasNext) { _listItems[n] = { "Next page >", nullptr }; _rowNext = n; n++; }

  setItems(_listItems, n);
}

// ── menu ───────────────────────────────────────────────

void WikipediaScreen::_showMenu() {
  _state = STATE_MENU;
  strcpy(_titleBuf, "Wikipedia");
  _langSub = _langName(_langCode());

  if (_readOnly) {
    _menuItems[0] = { "Favorite",      nullptr };
    _menuItems[1] = { "All Cached",    nullptr };
    _menuItems[2] = { "Search Cached", nullptr };
    _menuItems[3] = { "Language",      _langSub.c_str() };
    setItems(_menuItems, 4);
    return;
  }

  _menuItems[6] = { "Language", _langSub.c_str() };
  setItems(_menuItems, 7, _menuIndex);
}

void WikipediaScreen::_chooseLanguage() {
  InputSelectAction::Option opts[kWikiLangCount];
  for (uint8_t i = 0; i < kWikiLangCount; i++) {
    opts[i].label = kWikiLangs[i].name;
    opts[i].value = kWikiLangs[i].code;
  }
  String cur = _langCode();
  const char* chosen = InputSelectAction::popup("Wiki Language", opts, kWikiLangCount, cur.c_str());
  if (chosen) {
    Config.set(APP_CONFIG_WIKI_LANG, chosen);
    Config.save(Uni.Storage);
  }
  _showMenu();
}

// ── search (online) ────────────────────────────────────

void WikipediaScreen::_doSearch() {
  if (WiFi.status() != WL_CONNECTED) { ShowStatusAction::show("Not connected"); return; }

  String q = InputTextAction::popup("Search Wikipedia");
  if (InputTextAction::wasCancelled() || q.length() == 0) { render(); return; }

  _searchQuery = q;
  _runSearch(0);
}

void WikipediaScreen::_runSearch(int offset) {
  if (offset < 0) offset = 0;
  if (WiFi.status() != WL_CONNECTED) { ShowStatusAction::show("Not connected"); render(); return; }

  ShowStatusAction::show("Searching...", 0);
  String url = "https://" + _langCode() + ".wikipedia.org/w/api.php"
               "?action=query&list=search&format=json"
               "&srlimit=" + String((int)kPageSize) +
               "&sroffset=" + String(offset) +
               "&srsearch=" + _urlEncode(_searchQuery);
  String body;
  if (!_httpGet(url, body)) { ShowStatusAction::show("Failed"); render(); return; }

  _resultCount = 0;
  int from = 0;
  while (_resultCount < kPageSize) {
    int idx = body.indexOf("\"title\":\"", from);
    if (idx < 0) break;
    int start = idx + 9;
    int end = _strEnd(body, start);
    String raw = _jsonUnescape(body.substring(start, end));
    _titles[_resultCount]       = raw;
    _resultLabels[_resultCount] = _asciiFold(raw);
    _resultItems[_resultCount]  = { _resultLabels[_resultCount].c_str(), nullptr };
    _resultCount++;
    from = end + 1;
  }

  if (_resultCount == 0) {
    ShowStatusAction::show(offset > 0 ? "No more results" : "No results");
    if (offset > 0) { _runSearch(offset - kPageSize); return; }
    render();
    return;
  }

  if (offset == 0) { int n = Achievement.inc("wiki_first_search"); if (n == 1) Achievement.unlock("wiki_first_search"); }

  _resultHasMore = body.indexOf("\"continue\"") >= 0;
  _srOffset      = offset;

  _state = STATE_RESULTS;
  snprintf(_titleBuf, sizeof(_titleBuf), "Results %d-%d", offset + 1, offset + _resultCount);
  _navReadyAt = millis() + 200;
  _composePage(_resultItems, _resultCount, offset > 0, _resultHasMore);
}

void WikipediaScreen::_openResultByIdx(uint8_t dataIndex) {
  if (dataIndex >= _resultCount) return;
  String lang = _langCode();
  String path;
  if (_fetchAndCache(_titles[dataIndex], _resultLabels[dataIndex], lang, path))
    _viewArticle(path, lang);
}

// ── random ─────────────────────────────────────────────

void WikipediaScreen::_showRandom() {
  if (WiFi.status() != WL_CONNECTED) { ShowStatusAction::show("Not connected"); return; }

  ShowStatusAction::show("Loading...", 0);
  String lang = _langCode();
  String url = "https://" + lang + ".wikipedia.org/w/api.php"
               "?action=query&list=random&rnnamespace=0&rnlimit=1&format=json";
  String body;
  if (!_httpGet(url, body)) { ShowStatusAction::show("Failed"); return; }

  int idx = body.indexOf("\"title\":\"");
  if (idx < 0) { ShowStatusAction::show("Failed to select article"); return; }
  int start = idx + 9;
  String raw = _jsonUnescape(body.substring(start, _strEnd(body, start)));

  String path;
  if (_fetchAndCache(raw, _asciiFold(raw), lang, path)) {
    int n = Achievement.inc("wiki_random"); if (n == 1) Achievement.unlock("wiki_random");
    _viewArticle(path, lang);
  }
}

// ── on this day ────────────────────────────────────────

void WikipediaScreen::_showOnThisDay() {
  if (WiFi.status() != WL_CONNECTED) { ShowStatusAction::show("Not connected"); return; }

  struct tm tmv = {};
  if (!getLocalTime(&tmv, 0) || tmv.tm_year + 1900 < 2023) {
    ShowStatusAction::show("Time not set");
    return;
  }

  ShowStatusAction::show("Loading...", 0);
  String lang = _langCode();
  char ep[64];
  snprintf(ep, sizeof(ep), "/api/rest_v1/feed/onthisday/selected/%02d/%02d",
           tmv.tm_mon + 1, tmv.tm_mday);
  String url = "https://" + lang + ".wikipedia.org" + String(ep);
  String body;
  if (!_httpGet(url, body)) { ShowStatusAction::show("Not available for this language", 1800); return; }

  _otdCount = 0;
  int p = 0;
  while (_otdCount < kPageSize) {
    p = body.indexOf("\"text\":\"", p);
    if (p < 0) break;
    int ts = p + 8;
    int te = _strEnd(body, ts);
    String text = _asciiFold(_jsonUnescape(body.substring(ts, te)));

    int nextText = body.indexOf("\"text\":\"", te);
    int segEnd   = (nextText < 0) ? (int)body.length() : nextText;

    int year = 0;
    int yi = body.indexOf("\"year\":", ts);
    if (yi >= 0 && yi < segEnd) year = body.substring(yi + 7, yi + 12).toInt();

    String pageTitle;
    int ti = body.indexOf("\"title\":\"", ts);
    if (ti >= 0 && ti < segEnd) {
      int ps = ti + 9;
      pageTitle = _jsonUnescape(body.substring(ps, _strEnd(body, ps)));
    }

    String label = (year > 0) ? (String(year) + " - " + text) : text;
    _otdLabels[_otdCount] = label;
    _otdTitles[_otdCount] = pageTitle;
    _otdItems[_otdCount]  = { _otdLabels[_otdCount].c_str(), nullptr };
    _otdCount++;
    p = te + 1;
  }

  if (_otdCount == 0) { ShowStatusAction::show("Nothing for today"); return; }

  int n = Achievement.inc("wiki_onthisday"); if (n == 1) Achievement.unlock("wiki_onthisday");

  _state = STATE_ONTHISDAY;
  snprintf(_titleBuf, sizeof(_titleBuf), "On This Day %d/%d", tmv.tm_mon + 1, tmv.tm_mday);
  _navReadyAt = millis() + 200;
  setItems(_otdItems, _otdCount);
}

void WikipediaScreen::_openOtd(uint8_t index) {
  if (index >= _otdCount) return;
  if (_otdTitles[index].length() == 0) { ShowStatusAction::show("No linked article"); return; }
  String lang = _langCode();
  String path;
  if (_fetchAndCache(_otdTitles[index], _asciiFold(_otdTitles[index]), lang, path))
    _viewArticle(path, lang);
}

// ── cached / favorites (file list) ─────────────────────

void WikipediaScreen::_searchCached() {
  String q = InputTextAction::popup("Search Cached", _cacheFilter);
  if (InputTextAction::wasCancelled()) { render(); return; }
  q.trim();
  _flMode      = FL_CACHED;
  _cacheFilter = q;
  _loadFileList();
  _showFilePage(0);
}

void WikipediaScreen::_showAllCached() {
  _flMode      = FL_CACHED;
  _cacheFilter = "";
  _loadFileList();
  _showFilePage(0);
}

void WikipediaScreen::_showFavorites() {
  _flMode      = FL_FAV;
  _cacheFilter = "";
  _loadFileList();
  _showFilePage(0);
}

void WikipediaScreen::_loadFileList() {
  _flRel.clear();
  _flLabel.clear();
  _flFav.clear();

  if (_flMode == FL_FAV) {
    for (const String& rel : _favRel) {
      int slash = rel.lastIndexOf('/');
      String name = (slash >= 0) ? rel.substring(slash + 1) : rel;
      if (name.endsWith(".txt")) name = name.substring(0, name.length() - 4);
      _flRel.push_back(rel);
      _flLabel.push_back(name);
      _flFav.push_back(1);
    }
    return;
  }

  // FL_CACHED: list the current language's folder.
  String lang = _langCode();
  String dir  = String(WIKI_DIR) + "/" + lang;
  auto* entries = new IStorage::DirEntry[kFileScanCap];
  if (!entries) return;
  uint8_t n = Uni.Storage->listDir(dir.c_str(), entries, kFileScanCap);
  for (uint8_t i = 0; i < n; i++) {
    if (entries[i].isDir) continue;
    String name = entries[i].name;
    if (!name.endsWith(".txt")) continue;
    String label = name.substring(0, name.length() - 4);
    if (_cacheFilter.length() > 0 && !_containsCI(label, _cacheFilter)) continue;
    String rel = lang + "/" + name;
    _flRel.push_back(rel);
    _flLabel.push_back(label);
    _flFav.push_back(_favHas(rel) ? 1 : 0);
  }
  delete[] entries;
}

void WikipediaScreen::_showFilePage(int page) {
  int total = (int)_flRel.size();
  if (total == 0) {
    const char* msg = (_flMode == FL_FAV) ? "No favorites"
                    : (_cacheFilter.length() ? "No matches" : "No cached articles");
    ShowStatusAction::show(msg, 1500);
    _showMenu();
    return;
  }

  int maxPage = (total - 1) / kPageSize;
  if (page < 0)       page = 0;
  if (page > maxPage) page = maxPage;
  _flPage = (uint16_t)page;

  int startIdx = page * kPageSize;
  uint8_t dataCount = 0;
  for (int i = startIdx; i < total && dataCount < kPageSize; i++) {
    _pageItems[dataCount] = { _flLabel[i].c_str(), _flFav[i] ? "*" : nullptr };
    dataCount++;
  }

  _state = STATE_FILELIST;
  const char* base = (_flMode == FL_FAV) ? "Favorites" : "Cached";
  if (_flMode == FL_CACHED && _cacheFilter.length() > 0)
    snprintf(_titleBuf, sizeof(_titleBuf), "Cached: %s", _cacheFilter.c_str());
  else if (maxPage > 0)
    snprintf(_titleBuf, sizeof(_titleBuf), "%s (%d/%d)", base, page + 1, maxPage + 1);
  else
    strncpy(_titleBuf, base, sizeof(_titleBuf) - 1), _titleBuf[sizeof(_titleBuf) - 1] = '\0';

  _navReadyAt = millis() + 200;
  _composePage(_pageItems, dataCount, page > 0, page < maxPage);
}

void WikipediaScreen::_openFileAbs(uint16_t absIndex) {
  if (absIndex >= _flRel.size()) return;
  const String& rel = _flRel[absIndex];
  int slash = rel.indexOf('/');
  String lang = (slash > 0) ? rel.substring(0, slash) : _langCode();
  _viewArticle(String(WIKI_DIR) + "/" + rel, lang);
}

// ── view + read tracking ───────────────────────────────

void WikipediaScreen::_viewArticle(const String& path, const String& lang) {
  _recordRead(lang);
  Screen.push(new FileViewerScreen(path));   // word-wraps .txt
}

void WikipediaScreen::_recordRead(const String& lang) {
  int n = Achievement.inc("wiki_read_first");
  if (n == 1)  Achievement.unlock("wiki_read_first");
  if (n == 10) Achievement.unlock("wiki_read_10");

  // Polyglot: read articles in 2+ languages.
  String seen = Config.get("wiki_langs_read", "");
  if (!csvHas(seen, lang)) {
    seen = seen.length() ? (seen + "," + lang) : lang;
    Config.set("wiki_langs_read", seen);
    Config.save(Uni.Storage);
    if (csvCount(seen) >= 2) Achievement.unlock("wiki_polyglot");
  }
}

// ── context menu ───────────────────────────────────────

void WikipediaScreen::_openItemMenu(uint8_t displayIndex) {
  _ctxFrom = _state;

  if (_state == STATE_RESULTS) {
    uint8_t di = displayIndex - _rowDataStart;
    if (di >= _resultCount) { _holdFired = false; return; }
    _ctxResultIdx   = di;
    _ctxTitleForUrl = _titles[di];
    _ctxLabel       = _resultLabels[di];
    _ctxLang        = _langCode();
    _ctxIsFav       = false;
    _ctxPath = ""; _ctxRel = "";
  } else { // STATE_FILELIST
    uint16_t abs = (uint16_t)_flPage * kPageSize + (displayIndex - _rowDataStart);
    if (abs >= _flRel.size()) { _holdFired = false; return; }
    _ctxRel  = _flRel[abs];
    int slash = _ctxRel.indexOf('/');
    _ctxLang = (slash > 0) ? _ctxRel.substring(0, slash) : _langCode();
    _ctxPath = String(WIKI_DIR) + "/" + _ctxRel;
    _ctxLabel       = _flLabel[abs];
    _ctxTitleForUrl = _flLabel[abs];
    _ctxIsFav       = _favHas(_ctxRel);
  }

  _ctxCount = 0;
  _ctxItems[_ctxCount] = { "Open", nullptr };       _ctxActions[_ctxCount++] = CTX_OPEN;
  if (_ctxFrom == STATE_FILELIST) {
    _ctxFavLabel = _ctxIsFav ? "Unfavorite" : "Favorite";
    _ctxItems[_ctxCount] = { _ctxFavLabel.c_str(), nullptr }; _ctxActions[_ctxCount++] = CTX_FAV;
  }
  _ctxItems[_ctxCount] = { "Share QR", nullptr };   _ctxActions[_ctxCount++] = CTX_SHARE;
  _ctxItems[_ctxCount] = { "Cancel", nullptr };     _ctxActions[_ctxCount++] = CTX_CANCEL;

  _state = STATE_ITEM_MENU;
  strncpy(_titleBuf, _ctxLabel.c_str(), sizeof(_titleBuf) - 1);
  _titleBuf[sizeof(_titleBuf) - 1] = '\0';
  // No _navReadyAt here: the _holdFired drain in onUpdate consumes the
  // long-press release, like FileManagerScreen.
  setItems(_ctxItems, _ctxCount);
}

void WikipediaScreen::_returnFromContext() {
  if (_ctxFrom == STATE_RESULTS) {
    _state = STATE_RESULTS;
    snprintf(_titleBuf, sizeof(_titleBuf), "Results %d-%d",
             _srOffset + 1, _srOffset + _resultCount);
    _composePage(_resultItems, _resultCount, _srOffset > 0, _resultHasMore);
  } else {
    _loadFileList();   // picks up favorite changes
    _showFilePage(_flPage);
  }
}

void WikipediaScreen::_shareQr(const String& titleForUrl, const String& lang, const String& label) {
  String t = titleForUrl;
  t.replace(" ", "_");
  String url = "https://" + lang + ".wikipedia.org/wiki/" + _urlEncode(t);
  ShowQRCodeAction::show(label.c_str(), url.c_str());
  int n = Achievement.inc("wiki_share_qr"); if (n == 1) Achievement.unlock("wiki_share_qr");
}

// ── favorites ──────────────────────────────────────────

void WikipediaScreen::_favLoad() {
  _favRel.clear();
  String content = Uni.Storage ? Uni.Storage->readFile(FAV_FILE) : String();
  int pos = 0;
  while (pos < (int)content.length()) {
    int nl = content.indexOf('\n', pos);
    if (nl < 0) nl = content.length();
    String line = content.substring(pos, nl);
    line.trim();
    if (line.length() > 0) _favRel.push_back(line);
    pos = nl + 1;
  }
}

void WikipediaScreen::_favSave() {
  String content;
  for (const String& r : _favRel) content += r + "\n";
  Uni.Storage->makeDir(WIKI_DIR);
  Uni.Storage->writeFile(FAV_FILE, content.c_str());
}

bool WikipediaScreen::_favHas(const String& rel) {
  for (const String& r : _favRel) if (r == rel) return true;
  return false;
}

bool WikipediaScreen::_favToggle(const String& rel) {
  for (size_t i = 0; i < _favRel.size(); i++) {
    if (_favRel[i] == rel) { _favRel.erase(_favRel.begin() + i); _favSave(); return false; }
  }
  _favRel.push_back(rel);
  _favSave();
  return true;
}

// ── networking / parsing ───────────────────────────────

bool WikipediaScreen::_fetchAndCache(const String& rawTitle, const String& label,
                                     const String& lang, String& outPath) {
  if (WiFi.status() != WL_CONNECTED) { ShowStatusAction::show("Not connected"); render(); return false; }

  ShowStatusAction::show("Loading article...", 0);
  // No exchars: the API caps it at 1200, so requesting it truncates to the
  // intro. Omit it to get the full plaintext, then bound to 100 KB below.
  String url = "https://" + lang + ".wikipedia.org/w/api.php"
               "?action=query&prop=extracts"
               "&explaintext=1&exsectionformat=plain&redirects=1&exlimit=1"
               "&format=json&titles=" + _urlEncode(rawTitle);
  String body;
  if (!_httpGet(url, body)) { ShowStatusAction::show("Failed"); render(); return false; }

  String text = _extractField(body);
  if (text.length() == 0) { ShowStatusAction::show("Article content not available"); render(); return false; }
  if (text.length() > kMaxArticleBytes) text = text.substring(0, kMaxArticleBytes);

  Uni.Storage->makeDir(WIKI_DIR);
  Uni.Storage->makeDir((String(WIKI_DIR) + "/" + lang).c_str());
  outPath = String(WIKI_DIR) + "/" + lang + "/" + _sanitize(label) + ".txt";
  Uni.Storage->writeFile(outPath.c_str(), text.c_str());
  return true;
}

bool WikipediaScreen::_httpGet(const String& url, String& out) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("User-Agent", "ESP32-UniGeek");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  out = http.getString();
  http.end();
  return true;
}

String WikipediaScreen::_extractField(const String& body) {
  int ei = body.indexOf("\"extract\":\"");
  if (ei < 0) return "";
  int start = ei + 11;
  return _asciiFold(_jsonUnescape(body.substring(start, _strEnd(body, start))));
}

int WikipediaScreen::_strEnd(const String& s, int start) {
  int i = start;
  int len = s.length();
  while (i < len) {
    if (s[i] == '\\') { i += 2; continue; }
    if (s[i] == '"')  break;
    i++;
  }
  return i;
}

// ── language ───────────────────────────────────────────

String WikipediaScreen::_langCode() {
  return Config.get(APP_CONFIG_WIKI_LANG, APP_CONFIG_WIKI_LANG_DEFAULT);
}

const char* WikipediaScreen::_langName(const String& code) {
  for (uint8_t i = 0; i < kWikiLangCount; i++)
    if (code == kWikiLangs[i].code) return kWikiLangs[i].name;
  return kWikiLangs[0].name;
}

// ── string helpers ─────────────────────────────────────

bool WikipediaScreen::_containsCI(const String& hay, const String& needle) {
  String h = hay;    h.toLowerCase();
  String n = needle; n.toLowerCase();
  return h.indexOf(n) >= 0;
}

String WikipediaScreen::_urlEncode(const String& s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

void WikipediaScreen::_appendUtf8(String& out, uint32_t cp) {
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

String WikipediaScreen::_jsonUnescape(const String& s) {
  String out;
  int n = s.length();
  for (int i = 0; i < n; i++) {
    char c = s[i];
    if (c == '\\' && i + 1 < n) {
      char e = s[++i];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += ' ';  break;
        case 'r': case 'b': case 'f': break;
        case '"': out += '"';  break;
        case '\\': out += '\\'; break;
        case '/': out += '/';  break;
        case 'u':
          if (i + 4 < n) {
            long cp = strtol(s.substring(i + 1, i + 5).c_str(), nullptr, 16);
            i += 4;
            _appendUtf8(out, (uint32_t)cp);
          }
          break;
        default: out += e; break;
      }
    } else {
      out += c;
    }
  }
  return out;
}

String WikipediaScreen::_asciiFold(const String& s) {
  String out;
  int n = s.length();
  int i = 0;
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp;
    int adv;
    if (c < 0x80)                              { cp = c;                                                          adv = 1; }
    else if ((c & 0xE0) == 0xC0 && i + 1 < n)  { cp = ((c & 0x1Fu) << 6) | (s[i+1] & 0x3F);                       adv = 2; }
    else if ((c & 0xF0) == 0xE0 && i + 2 < n)  { cp = ((c & 0x0Fu) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F); adv = 3; }
    else if ((c & 0xF8) == 0xF0 && i + 3 < n)  { cp = ((c & 0x07u) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F); adv = 4; }
    else { i++; continue; }
    i += adv;

    if (cp == '\n')      { out += '\n'; continue; }
    if (cp == '\t')      { out += ' ';  continue; }
    if (cp < 32)         continue;
    if (cp < 128)        { out += (char)cp; continue; }

    const char* tr = translitCp(cp);
    out += tr ? tr : "?";
  }
  return out;
}

String WikipediaScreen::_sanitize(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == ' ' || c == '.') out += c;
    else out += '_';
  }
  out.trim();
  if (out.length() == 0)  out = "article";
  if (out.length() > 40)  out = out.substring(0, 40);
  return out;
}
