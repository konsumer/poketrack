#pragma once

// Stand-in for wclap-bridge/modules/webview-gui's clap-webview-gui.h, with
// the same public API but no native GUI backend (no GTK/WebKit/WebView2).
// poketrack never hosts a CLAP plugin's GUI, so wclap-bridge's real
// webview-gui — which pulls in a hard runtime dependency on GTK+WebKit on
// Linux (bad for a framebuffer-only handheld build with no desktop
// installed) — is unnecessary weight. This tells any plugin/host asking
// "do you support a GUI api" simply "no", which is exactly the behavior
// poketrack already relies on (unmapped params only, ADD-row workflow).

#include "clap/clap.h"

#include <cstring>

namespace webview_gui {

static constexpr const char CLAP_EXT_WEBVIEW[] = "clap.webview/3";
static constexpr const char CLAP_WINDOW_API_WEBVIEW[] = "webview";
struct clap_plugin_webview {
  int32_t(CLAP_ABI *get_uri)(const clap_plugin_t *plugin, char *uri, uint32_t uri_capacity);
  bool(CLAP_ABI *get_resource)(const clap_plugin_t *plugin, const char *path, char *mime, uint32_t mime_capacity, const clap_ostream_t *stream);
  bool(CLAP_ABI *receive)(const clap_plugin_t *plugin, const void *buffer, uint32_t size);
};
struct clap_host_webview {
  bool(CLAP_ABI *send)(const clap_host_t *host, const void *buffer, uint32_t size);
};

struct ClapWebviewGui {
  clap_plugin_gui *extPluginGui;
  const clap_host_webview *extHostWebview;

  ClapWebviewGui(const clap_plugin * = nullptr, const clap_host * = nullptr) {
    extPluginGui = &pluginGuiProxy;
    extHostWebview = nullptr;
  }

  void init(const clap_plugin *, const clap_host *) {}

  bool isApiSupported(const char *, bool) { return false; }
  bool getPreferredApi(const char **, bool *) { return false; }
  bool create(const char *, bool) { return false; }
  void destroy() {}
  bool setScale(double) { return true; }
  bool getSize(uint32_t *w, uint32_t *h) {
    *w = 400;
    *h = 250;
    return true;
  }
  bool canResize() { return false; }
  bool getResizeHints(clap_gui_resize_hints_t *hints) {
    *hints = {false, false, false, 0, 0};
    return true;
  }
  bool adjustSize(uint32_t *, uint32_t *) { return false; }
  bool setSize(uint32_t, uint32_t) { return false; }
  bool setParent(const clap_window *) { return false; }
  bool setTransient(const clap_window *) { return false; }
  void suggestTitle(const char *) {}
  bool show() { return false; }
  bool hide() { return false; }

  bool send(const void *, size_t) { return false; }

private:
  static bool gui_is_api_supported(const clap_plugin *, const char *, bool) { return false; }
  static bool gui_get_preferred_api(const clap_plugin *, const char **, bool *) { return false; }
  static bool gui_create(const clap_plugin *, const char *, bool) { return false; }
  static void gui_destroy(const clap_plugin *) {}
  static bool gui_set_scale(const clap_plugin *, double) { return true; }
  static bool gui_get_size(const clap_plugin *, uint32_t *w, uint32_t *h) {
    *w = 400;
    *h = 250;
    return true;
  }
  static bool gui_can_resize(const clap_plugin *) { return false; }
  static bool gui_get_resize_hints(const clap_plugin *, clap_gui_resize_hints_t *hints) {
    *hints = {false, false, false, 0, 0};
    return true;
  }
  static bool gui_adjust_size(const clap_plugin *, uint32_t *, uint32_t *) { return false; }
  static bool gui_set_size(const clap_plugin *, uint32_t, uint32_t) { return false; }
  static bool gui_set_parent(const clap_plugin *, const clap_window *) { return false; }
  static bool gui_set_transient(const clap_plugin *, const clap_window *) { return false; }
  static void gui_suggest_title(const clap_plugin *, const char *) {}
  static bool gui_show(const clap_plugin *) { return false; }
  static bool gui_hide(const clap_plugin *) { return false; }

  clap_plugin_gui pluginGuiProxy{
      gui_is_api_supported,
      gui_get_preferred_api,
      gui_create,
      gui_destroy,
      gui_set_scale,
      gui_get_size,
      gui_can_resize,
      gui_get_resize_hints,
      gui_adjust_size,
      gui_set_size,
      gui_set_parent,
      gui_set_transient,
      gui_suggest_title,
      gui_show,
      gui_hide};
};

}  // namespace webview_gui
