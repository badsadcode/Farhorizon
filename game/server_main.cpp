// farhorizon_server: the window you leave running on the machine that hosts a co-op game. It needs no Vulkan and no
// graphics card to speak of: SDL's own 2D renderer, including its software one, draws it.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/log.h"
#include "engine/debug/overlay.h"
#include "engine/ui/font_atlas.h"
#include "game/net/dedicated_server.h"

using namespace fh;
using json = nlohmann::json;

namespace {

const OverlayColor kBack{14, 12, 10, 255}, kPanel{28, 24, 19, 255}, kLine{92, 72, 44, 255}, kText{226, 200, 158, 255};
const OverlayColor kDim{142, 124, 96, 255}, kAccent{240, 170, 70, 255}, kAccentDark{84, 58, 26, 255}, kGood{130, 210, 130, 255};
const OverlayColor kBad{235, 110, 90, 255}, kField{9, 8, 7, 255};

enum class Tab { Setup, Players, Log, Help };
enum class Field { None, Name, Port, Password };

struct LogLine {
  log::Level level;
  std::string text;
};
std::deque<LogLine> logLines;

struct App {
  net::ServerConfig config;
  std::string portText = std::to_string(net::kDefaultPort);
  Tab tab = Tab::Setup;
  Field focus = Field::None;
  std::string status;
  bool statusBad = false;
  bool showPassword = false;
  std::vector<std::string> addresses;
  // This frame's mouse.
  float mx = 0, my = 0;
  bool clicked = false;
  OverlayBuilder o;
};

bool hit(const App& a, float x, float y, float w, float h) { return a.mx >= x && a.mx < x + w && a.my >= y && a.my < y + h; }

bool button(App& a, float x, float y, float w, float h, const std::string& label, bool enabled = true, bool strong = false) {
  const bool over = enabled && hit(a, x, y, w, h);
  const OverlayColor fill = !enabled ? kPanel : strong ? (over ? kAccent : OverlayColor{196, 136, 54, 255}) : (over ? kAccentDark : kPanel);
  a.o.panel(x, y, w, h, fill, enabled ? kAccent : kLine, 6.0f);
  const OverlayColor text = !enabled ? kDim : strong ? OverlayColor{20, 14, 8, 255} : kText;
  a.o.textPx(x + w / 2, y + (h - OverlayBuilder::lineHeight(18)) / 2, label, text, 18, FontFace::Mono, TextAlign::Center);
  return over && a.clicked;
}

void textField(App& a, Field id, float x, float y, float w, const std::string& label, const std::string& value, bool enabled, bool secret = false) {
  a.o.textPx(x, y, label, kDim, 16);
  const float top = y + 22, h = 36;
  const bool focused = a.focus == id && enabled;
  a.o.rect(x, top, w, h, kField);
  a.o.panel(x, top, w, h, OverlayColor{0, 0, 0, 0}, focused ? kAccent : kLine, 0.0f);
  std::string shown = secret ? std::string(value.size(), '*') : value;
  a.o.textPx(x + 10, top + 8, shown + (focused && (SDL_GetTicks() / 500) % 2 == 0 ? "_" : ""), enabled ? kText : kDim, 18);
  if (enabled && a.clicked && hit(a, x, top, w, h)) a.focus = id;
}

void drawSetup(App& a, net::DedicatedServer& server, const std::filesystem::path& savePath, float w) {
  const bool on = server.running();
  float y = 96;
  textField(a, Field::Name, 28, y, w - 56, "SERVER NAME", a.config.name, !on);
  y += 72;
  textField(a, Field::Port, 28, y, 150, "UDP PORT", a.portText, !on);
  a.o.textPx(200, y + 30, "Your friends connect here. The HELP tab explains the router.", kDim, 15);
  y += 72;
  textField(a, Field::Password, 28, y, 320, "PASSWORD (REQUIRED)", a.config.password, !on, !a.showPassword);
  if (button(a, 362, y + 22, 90, 36, a.showPassword ? "HIDE" : "SHOW")) a.showPassword = !a.showPassword;
  y += 72;
  a.o.textPx(28, y, "PLAYERS", kDim, 16);
  if (button(a, 28, y + 22, 36, 36, "-", !on && a.config.maxPlayers > 1)) --a.config.maxPlayers;
  a.o.textPx(90, y + 30, std::to_string(a.config.maxPlayers), on ? kDim : kText, 20, FontFace::Mono, TextAlign::Center);
  if (button(a, 116, y + 22, 36, 36, "+", !on && a.config.maxPlayers < GameSession::kMaxPlayers)) ++a.config.maxPlayers;
  y += 88;

  if (!on) {
    if (button(a, 28, y, 220, 48, "START SERVER", true, true)) {
      const int port = std::atoi(a.portText.c_str());
      std::string why;
      if (port < 1024 || port > 65535) {
        a.status = "The port must be a number from 1024 to 65535";
        a.statusBad = true;
      } else {
        a.config.port = std::uint16_t(port);
        a.statusBad = !server.start(a.config, savePath, &why);
        a.status = a.statusBad ? why : "Running. Players can join now.";
        if (!a.statusBad) a.focus = Field::None;
      }
    }
  } else {
    if (button(a, 28, y, 220, 48, "STOP AND SAVE")) {
      server.stop();
      a.status = "Stopped. The world is saved.";
      a.statusBad = false;
    }
    if (button(a, 262, y, 160, 48, "SAVE NOW")) {
      std::string why;
      a.statusBad = !server.saveNow(&why);
      a.status = a.statusBad ? why : "World saved.";
    }
  }
  y += 62;
  if (!a.status.empty()) a.o.textPx(28, y, a.status, a.statusBad ? kBad : kGood, 17);
  y += 30;
  a.o.textPx(28, y, "To join, a player chooses Join game in the main menu and types:", kDim, 15);
  a.o.textPx(28, y + 22, std::format("<your public address>:{}   and the password", a.portText), kText, 15);
  a.o.textPx(28, y + 46, std::format("The world is saved in {}", savePath.string()), kDim, 14);
}

void drawPlayers(App& a, net::DedicatedServer& server, float w) {
  float y = 96;
  if (!server.running()) {
    a.o.textPx(28, y, "The server is not running.", kDim, 18);
    return;
  }
  const auto players = server.players();
  a.o.textPx(28, y, std::format("{} of {} connected", players.size(), a.config.maxPlayers), kText, 18);
  a.o.textPx(w - 28, y, server.simulating() ? std::format("World time {:.0f} s", server.worldTime()) : "World paused: nobody is here", server.simulating() ? kGood : kDim, 16,
             FontFace::Mono, TextAlign::Right);
  y += 40;
  a.o.textPx(28, y, "NAME", kDim, 15);
  a.o.textPx(320, y, "WHERE", kDim, 15);
  y += 24;
  for (const auto& p : players) {
    a.o.rect(28, y, w - 56, 44, kPanel);
    a.o.textPx(40, y + 12, p.name, kText, 18);
    a.o.textPx(320, y + 12, p.where, p.pilot ? kAccent : kText, 17);
    if (button(a, w - 128, y + 6, 88, 32, "REMOVE")) server.kick(p.id);
    y += 50;
  }
  if (players.empty()) a.o.textPx(28, y, "Nobody yet.", kDim, 17);
}

void drawLog(App& a, float w, float h) {
  const float px = 15, lh = OverlayBuilder::lineHeight(px) + 2;
  const int fit = int((h - 110) / lh);
  float y = 96;
  const size_t first = logLines.size() > size_t(fit) ? logLines.size() - size_t(fit) : 0;
  for (size_t i = first; i < logLines.size(); ++i, y += lh) {
    const LogLine& l = logLines[i];
    const OverlayColor c = l.level == log::Level::Error ? kBad : l.level == log::Level::Warn ? kAccent : kText;
    std::string text = l.text;
    while (!text.empty() && OverlayBuilder::measure(text, px) > w - 56) text.pop_back();
    a.o.textPx(28, y, text, c, px);
  }
}

void drawHelp(App& a) {
  float y = 96;
  auto heading = [&](const std::string& text) {
    a.o.textPx(28, y, text, kAccent, 18);
    y += 28;
  };
  auto line = [&](const std::string& text, OverlayColor c = kText) {
    a.o.textPx(28, y, text, c, 16);
    y += 23;
  };
  std::string here;
  for (const std::string& addr : a.addresses) here += (here.empty() ? "" : "  or  ") + addr;
  if (here.empty()) here = "not found: run ipconfig and look for the IPv4 address";
  heading("HOW TO LET A FRIEND JOIN OVER THE INTERNET");
  line("1. On the SETUP tab, type a password and press START SERVER.");
  line("2. If Windows Firewall asks about this program, allow it.");
  line("3. Open your router's settings, and find 'port forwarding'. Add one rule:");
  line(std::format("     protocol UDP,  port {},  to this PC: {}", a.portText, here), kAccent);
  line("4. Find your public address: search the web for 'what is my IP'.");
  line("5. Send your friend that address, the port and the password.");
  y += 14;
  heading("YOUR FRIEND JOINS WITH");
  line(std::format("Join game, in the game's main menu. Server address: <your public address>:{}", a.portText), kAccent);
  y += 14;
  heading("IF IT DOESN'T CONNECT");
  line("Open the LOG tab while your friend tries. If 'connection from' appears, the router works.");
  line("If nothing appears: check the router rule (UDP, not TCP), the firewall, and the address.");
  line("Some internet providers don't give you a public address. Ask yours if nothing else helps.");
}

void drawAll(App& a, net::DedicatedServer& server, const std::filesystem::path& savePath, float w, float h) {
  a.o.clear();
  a.o.rect(0, 0, w, h, kBack);
  a.o.textPx(28, 14, "FARHORIZON SERVER", kAccent, 24, FontFace::Sans);
  const bool on = server.running();
  a.o.textPx(w - 28, 18, on ? (server.simulating() ? "RUNNING" : "RUNNING - WAITING FOR PLAYERS") : "STOPPED", on ? kGood : kDim, 17, FontFace::Mono, TextAlign::Right);
  const char* names[] = {"SETUP", "PLAYERS", "LOG", "HELP"};
  float x = 28;
  for (int i = 0; i < 4; ++i) {
    const bool sel = int(a.tab) == i;
    if (sel) a.o.rect(x, 74, 120, 3, kAccent);
    a.o.textPx(x + 60, 50, names[i], sel ? kAccent : (hit(a, x, 44, 120, 34) ? kText : kDim), 18, FontFace::Mono, TextAlign::Center);
    if (a.clicked && hit(a, x, 44, 120, 34)) a.tab = Tab(i);
    x += 130;
  }
  a.o.rect(28, 77, w - 56, 1, kLine);
  if (a.tab == Tab::Setup) drawSetup(a, server, savePath, w);
  else if (a.tab == Tab::Players) drawPlayers(a, server, w);
  else if (a.tab == Tab::Log) drawLog(a, w, h);
  else drawHelp(a);
}

std::filesystem::path findContent() {
  std::error_code ec;
  if (const char* base = SDL_GetBasePath()) {
    const std::filesystem::path root(base);
    if (std::filesystem::exists(root / "content" / "asset_manifest.json", ec)) {
      std::filesystem::current_path(root, ec);  // settings and saves belong beside the program
      return root / "content";
    }
  }
  return FH_DEFAULT_CONTENT_DIR;
}

void loadConfig(App& a, const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) return;
  const json j = json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return;
  a.config.name = j.value("name", a.config.name);
  a.portText = std::to_string(j.value("port", int(net::kDefaultPort)));
  a.config.password = j.value("password", std::string());
  a.config.maxPlayers = std::clamp(j.value("maxPlayers", 4), 1, GameSession::kMaxPlayers);
}

void saveConfig(const App& a, const std::filesystem::path& path) {
  std::ofstream out(path);
  out << json{{"name", a.config.name}, {"port", std::atoi(a.portText.c_str())}, {"password", a.config.password}, {"maxPlayers", a.config.maxPlayers}}.dump(2);
}

}  // namespace

int main(int argc, char** argv) {
  App a;
  std::filesystem::path contentDir = findContent();
  a.addresses = net::localAddresses();
  std::filesystem::path configPath = "server_config.json", savePath = "saves/server_world.json", screenshot;
  log::setLogFile("farhorizon_server.log");
  log::setSink([](log::Level level, const char* subsystem, const std::string& message) {
    logLines.push_back({level, std::format("[{}] {}", subsystem, message)});
    if (logLines.size() > 2000) logLines.pop_front();
  });
  loadConfig(a, configPath);

  bool autoStart = false;
  double exitAfter = -1, shotAfter = 1.0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (arg == "--start") autoStart = true;
    else if (arg == "--port") a.portText = next();
    else if (arg == "--password") a.config.password = next();
    else if (arg == "--name") a.config.name = next();
    else if (arg == "--max-players") a.config.maxPlayers = std::clamp(std::atoi(next()), 1, GameSession::kMaxPlayers);
    else if (arg == "--save") savePath = next();
    else if (arg == "--config") configPath = next();
    else if (arg == "--content") contentDir = next();
    else if (arg == "--exit-after") exitAfter = std::atof(next());   // testing: seconds until it closes itself
    else if (arg == "--screenshot") screenshot = next();             // testing: a BMP of the window
    else if (arg == "--shot-after") shotAfter = std::atof(next());
    else if (arg == "--tab") a.tab = Tab(std::clamp(std::atoi(next()), 0, 3));
    else {
      std::fprintf(stderr, "unknown option %s\nfarhorizon_server [--start] [--port n] [--password p] [--name s] [--max-players n] [--save file]\n", arg.c_str());
      return 1;
    }
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "could not open a window: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("FARHORIZON server", 960, 660, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    std::fprintf(stderr, "could not open a window: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);
  log::info("server", "window drawn by SDL's '{}' renderer", SDL_GetRendererName(renderer));

  FontAtlas font;
  std::string fontError;
  SDL_Texture* atlas = nullptr;
  if (font.build({contentDir / "fonts" / "ShareTechMono-Regular.ttf", contentDir / "fonts" / "ShareTech-Regular.ttf"}, &fontError)) {
    OverlayBuilder::setFont(&font);
    atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(font.width()), int(font.height()));
    SDL_UpdateTexture(atlas, nullptr, font.pixels().data(), int(font.width()) * 4);
    SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_LINEAR);
  } else {
    log::error("server", "could not load the fonts: {}", fontError);
  }

  net::DedicatedServer server(contentDir);
  if (autoStart) {
    a.config.port = std::uint16_t(std::atoi(a.portText.c_str()));
    std::string why;
    a.statusBad = !server.start(a.config, savePath, &why);
    a.status = a.statusBad ? why : "Running. Players can join now.";
  }
  SDL_StartTextInput(window);

  std::vector<SDL_Vertex> verts;
  Uint64 last = SDL_GetPerformanceCounter();
  double elapsed = 0;
  bool quit = false, shot = false;
  while (!quit) {
    a.clicked = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_EVENT_QUIT:
          quit = true;
          break;
        case SDL_EVENT_MOUSE_MOTION:
          a.mx = e.motion.x;
          a.my = e.motion.y;
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          if (e.button.button == SDL_BUTTON_LEFT) {
            a.mx = e.button.x;
            a.my = e.button.y;
            a.clicked = true;
          }
          break;
        case SDL_EVENT_TEXT_INPUT: {
          std::string* target = a.focus == Field::Name ? &a.config.name : a.focus == Field::Port ? &a.portText : a.focus == Field::Password ? &a.config.password : nullptr;
          if (!target || server.running()) break;
          for (const char* c = e.text.text; *c; ++c) {
            if (a.focus == Field::Port && (*c < '0' || *c > '9')) continue;
            if (target->size() < (a.focus == Field::Port ? 5u : 40u)) target->push_back(*c);
          }
          break;
        }
        case SDL_EVENT_KEY_DOWN: {
          std::string* target = a.focus == Field::Name ? &a.config.name : a.focus == Field::Port ? &a.portText : a.focus == Field::Password ? &a.config.password : nullptr;
          if (e.key.key == SDLK_BACKSPACE && target && !target->empty() && !server.running()) target->pop_back();
          if (e.key.key == SDLK_V && (e.key.mod & SDL_KMOD_CTRL) && target && !server.running()) {
            if (char* clip = SDL_GetClipboardText()) {
              for (const char* c = clip; *c && target->size() < 40; ++c)
                if (*c >= ' ') target->push_back(*c);
              SDL_free(clip);
            }
          }
          if (e.key.key == SDLK_TAB && a.tab == Tab::Setup)
            a.focus = a.focus == Field::Name ? Field::Port : a.focus == Field::Port ? Field::Password : Field::Name;
          break;
        }
        default:
          break;
      }
    }

    const Uint64 now = SDL_GetPerformanceCounter();
    const double dt = double(now - last) / double(SDL_GetPerformanceFrequency());
    last = now;
    elapsed += dt;
    server.update(dt);

    int w = 960, h = 660;
    SDL_GetWindowSize(window, &w, &h);
    drawAll(a, server, savePath, float(w), float(h));

    const auto& source = a.o.vertices();
    verts.resize(source.size());
    for (size_t i = 0; i < source.size(); ++i) {
      const OverlayVertex& v = source[i];
      verts[i].position = {v.x, v.y};
      verts[i].color = {float(v.r) / 255.0f, float(v.g) / 255.0f, float(v.b) / 255.0f, float(v.a) / 255.0f};
      verts[i].tex_coord = {v.u, v.v};
    }
    SDL_SetRenderDrawColor(renderer, kBack.r, kBack.g, kBack.b, 255);
    SDL_RenderClear(renderer);
    if (!verts.empty()) SDL_RenderGeometry(renderer, atlas, verts.data(), int(verts.size()), nullptr, 0);

    if (!screenshot.empty() && !shot && elapsed >= shotAfter) {
      if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) {
        SDL_SaveBMP(s, screenshot.string().c_str());
        SDL_DestroySurface(s);
      }
      shot = true;
    }
    SDL_RenderPresent(renderer);
    if (exitAfter >= 0 && elapsed >= exitAfter) quit = true;
    if (!SDL_GetRenderVSync(renderer, nullptr)) SDL_Delay(5);
  }

  saveConfig(a, configPath);
  server.stop();
  log::setSink(nullptr);
  OverlayBuilder::setFont(nullptr);
  if (atlas) SDL_DestroyTexture(atlas);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
