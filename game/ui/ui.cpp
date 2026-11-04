#include "game/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace fh {

void Ui::begin(OverlayBuilder& o, const UiTheme& theme, float width, float height, float userScale, const UiInput& in,
               double time) {
  o_ = &o;
  theme_ = &theme;
  w_ = width;
  h_ = height;
  scale_ = std::clamp(height / 1080.0f, 0.6f, 2.5f) * userScale;
  in_ = in;
  time_ = time;
  lastCount_ = count_;
  count_ = 0;
  if (lastCount_ > 0) {
    if (in_.up) focus_ = (focus_ + lastCount_ - 1) % lastCount_;
    if (in_.down) focus_ = (focus_ + 1) % lastCount_;
    focus_ = std::clamp(focus_, 0, lastCount_ - 1);
  }
  if (!in_.mouseDown) dragging_ = -1;
}

void Ui::end() {
  if (count_ > 0) focus_ = std::clamp(focus_, 0, count_ - 1);
}

bool Ui::hovered(float x, float y, float w, float h) const {
  return in_.mouseX >= x && in_.mouseX <= x + w && in_.mouseY >= y && in_.mouseY <= y + h;
}

void Ui::backdrop(float alpha) { o_->rect(0, 0, w_, h_, theme_->backdrop.withAlpha(alpha)); }

void Ui::leftShade(float frac) {
  const int strips = 32;
  const float total = w_ * frac;
  for (int i = 0; i < strips; ++i) {
    const float t = float(i) / float(strips);
    const float a = 0.82f * (1.0f - t) * (1.0f - t);
    o_->rect(total * t, 0, total / float(strips) + 1.0f, h_, OverlayColor{0, 0, 0, 255}.withAlpha(a));
  }
}

void Ui::panel(float x, float y, float w, float h) {
  o_->panel(s(x), s(y), s(w), s(h), theme_->panel, theme_->panelBorder, s(theme_->chamfer), std::max(1.0f, s(theme_->border)));
}

void Ui::label(float x, float y, std::string_view text, float px, OverlayColor c, FontFace face, TextAlign align) {
  o_->textPx(s(x), s(y), text, c, s(px), face, align);
}

void Ui::spacedTitle(float x, float y, std::string_view text, float px, OverlayColor c) {
  float pen = s(x);
  const float gap = s(px) * theme_->letterSpacing;
  for (size_t i = 0; i < text.size(); ++i) {
    const std::string_view ch = text.substr(i, 1);
    pen += o_->textPx(pen, s(y), ch, c, s(px), FontFace::Sans) + gap;
  }
}

void Ui::hintBar(std::string_view text) {
  const float y = h_ - s(46);
  o_->line(s(40), y - s(10), w_ - s(40), y - s(10), 1.0f, theme_->textDim.withAlpha(0.5f));
  o_->textPx(s(40), y, text, theme_->textDim, s(theme_->small));
}

void Ui::rowFrame(float x, float y, float w, float h, bool focused) {
  if (!focused) return;
  o_->rect(x, y, w, h, theme_->highlight);
  o_->rect(x, y, std::max(2.0f, s(3)), h, theme_->text);  // focus bar on the left
}

bool Ui::button(float x, float y, float w, float h, std::string_view text, bool enabled) {
  const int id = nextFocus();
  const float X = s(x), Y = s(y), W = s(w), H = s(h);
  const bool hot = enabled && hovered(X, Y, W, H);
  if (hot && in_.mouseMoved) focus_ = id;
  const bool focused = focus_ == id;
  const OverlayColor border = !enabled ? theme_->textDim.withAlpha(0.4f) : focused ? theme_->textBright : theme_->panelBorder;
  o_->panel(X, Y, W, H, focused && enabled ? theme_->highlight : theme_->panel, border, s(8), std::max(1.0f, s(1.5f)));
  o_->textPx(X + W * 0.5f, Y + (H - OverlayBuilder::lineHeight(s(theme_->body))) * 0.5f, text,
             enabled ? (focused ? theme_->textBright : theme_->text) : theme_->textDim, s(theme_->body), FontFace::Mono,
             TextAlign::Center);
  return enabled && ((focused && in_.accept) || (hot && in_.mousePressed));
}

bool Ui::menuItem(float x, float y, float w, std::string_view text, bool enabled) {
  const int id = nextFocus();
  const float X = s(x), Y = s(y), W = s(w), H = s(theme_->rowHeight + 6);
  const bool hot = enabled && hovered(X, Y, W, H);
  if (hot && in_.mouseMoved) focus_ = id;
  const bool focused = focus_ == id;
  const float px = s(theme_->heading);
  const float ty = Y + (H - OverlayBuilder::lineHeight(px, FontFace::Sans)) * 0.5f;
  if (focused && enabled) {
    o_->rect(X, Y, W, H, theme_->highlight);
    o_->rect(X, Y, std::max(2.0f, s(4)), H, theme_->text);
    const float blink = std::fmod(time_, 1.0) < 0.55 ? 1.0f : 0.35f;
    o_->textPx(X + W - s(18), ty, "<", theme_->textBright.withAlpha(blink), px, FontFace::Sans, TextAlign::Right);
  }
  const OverlayColor c = !enabled ? theme_->textDim.withAlpha(0.45f) : focused ? theme_->textBright : theme_->text;
  o_->textPx(X + s(22), ty, text, c, px, FontFace::Sans);
  return enabled && ((focused && in_.accept) || (hot && in_.mousePressed));
}

bool Ui::sliderRow(float x, float y, float w, std::string_view text, float& value, float lo, float hi, float step,
                   std::string_view valueText) {
  const int id = nextFocus();
  const float X = s(x), Y = s(y), W = s(w), H = s(theme_->rowHeight);
  const bool hot = hovered(X, Y, W, H);
  if (hot && in_.mouseMoved) focus_ = id;
  const bool focused = focus_ == id;
  rowFrame(X, Y, W, H, focused);
  const float px = s(theme_->body);
  const float ty = Y + (H - OverlayBuilder::lineHeight(px)) * 0.5f;
  o_->textPx(X + s(16), ty, text, focused ? theme_->textBright : theme_->text, px);
  // Bar on the right half.
  const float bx = X + W * 0.48f, bw = W * 0.38f, by = Y + H * 0.5f - s(3), bh = s(6);
  o_->rect(bx, by, bw, bh, theme_->textDim.withAlpha(0.35f));
  const float t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
  o_->rect(bx, by, bw * t, bh, focused ? theme_->textBright : theme_->text);
  for (int k = 0; k <= 10; ++k)  // ticks
    o_->rect(bx + bw * float(k) / 10.0f, by + bh + s(3), 1.0f, s(k % 5 == 0 ? 6 : 3), theme_->textDim);
  o_->rect(bx + bw * t - s(2), by - s(6), s(4), bh + s(12), theme_->textBright);
  o_->textPx(X + W - s(14), ty, valueText, theme_->text, px, FontFace::Mono, TextAlign::Right);
  const float before = value;
  if (focused && in_.left) value -= step;
  if (focused && in_.right) value += step;
  if (in_.mousePressed && hovered(bx - s(8), Y, bw + s(16), H)) dragging_ = id;
  if (dragging_ == id && in_.mouseDown) {
    const float f = std::clamp((in_.mouseX - bx) / bw, 0.0f, 1.0f);
    value = lo + f * (hi - lo);
    value = std::round(value / step) * step;
  }
  value = std::clamp(value, lo, hi);
  return std::fabs(value - before) > 1e-6f;
}

bool Ui::toggleRow(float x, float y, float w, std::string_view text, bool& value) {
  const int id = nextFocus();
  const float X = s(x), Y = s(y), W = s(w), H = s(theme_->rowHeight);
  const bool hot = hovered(X, Y, W, H);
  if (hot && in_.mouseMoved) focus_ = id;
  const bool focused = focus_ == id;
  rowFrame(X, Y, W, H, focused);
  const float px = s(theme_->body);
  const float ty = Y + (H - OverlayBuilder::lineHeight(px)) * 0.5f;
  o_->textPx(X + s(16), ty, text, focused ? theme_->textBright : theme_->text, px);
  // Two-state switch: [ ON | OFF ]
  const float sw = s(110), sx = X + W - sw - s(14), sy = Y + H * 0.5f - s(13), sh = s(26);
  o_->panel(sx, sy, sw, sh, theme_->panel, theme_->panelBorder, s(5), 1.0f);
  const float half = sw * 0.5f;
  o_->rect(value ? sx + s(3) : sx + half, sy + s(3), half - s(3), sh - s(6), value ? theme_->text : theme_->textDim.withAlpha(0.35f));
  o_->textPx(sx + half * 0.5f, sy + (sh - OverlayBuilder::lineHeight(s(theme_->small))) * 0.5f, "ON",
             value ? theme_->panel.withAlpha(1.0f) : theme_->textDim, s(theme_->small), FontFace::Mono, TextAlign::Center);
  o_->textPx(sx + half * 1.5f, sy + (sh - OverlayBuilder::lineHeight(s(theme_->small))) * 0.5f, "OFF",
             value ? theme_->textDim : theme_->textBright, s(theme_->small), FontFace::Mono, TextAlign::Center);
  const bool before = value;
  if (focused && (in_.accept || in_.left || in_.right)) value = !value;
  if (hot && in_.mousePressed) value = !value;
  return value != before;
}

bool Ui::choiceRow(float x, float y, float w, std::string_view text, int& index, const std::vector<std::string>& options) {
  const int id = nextFocus();
  const float X = s(x), Y = s(y), W = s(w), H = s(theme_->rowHeight);
  const bool hot = hovered(X, Y, W, H);
  if (hot && in_.mouseMoved) focus_ = id;
  const bool focused = focus_ == id;
  rowFrame(X, Y, W, H, focused);
  const float px = s(theme_->body);
  const float ty = Y + (H - OverlayBuilder::lineHeight(px)) * 0.5f;
  o_->textPx(X + s(16), ty, text, focused ? theme_->textBright : theme_->text, px);
  const int n = int(options.size());
  if (n == 0) return false;
  index = std::clamp(index, 0, n - 1);
  const float cx = X + W * 0.48f + W * 0.25f;
  const OverlayColor arrow = focused ? theme_->textBright : theme_->textDim;
  o_->textPx(X + W * 0.48f, ty, "<", arrow, px);
  o_->textPx(X + W - s(14), ty, ">", arrow, px, FontFace::Mono, TextAlign::Right);
  o_->textPx(cx, ty, options[size_t(index)], theme_->text, px, FontFace::Mono, TextAlign::Center);
  const int before = index;
  if (focused && in_.left) index = (index + n - 1) % n;
  if (focused && (in_.right || in_.accept)) index = (index + 1) % n;
  if (hot && in_.mousePressed) index = in_.mouseX < cx ? (index + n - 1) % n : (index + 1) % n;
  return index != before;
}

bool Ui::keyRow(float x, float y, float w, std::string_view text, std::string_view keys, bool listening) {
  const int id = nextFocus();
  const float X = s(x), Y = s(y), W = s(w), H = s(theme_->rowHeight);
  const bool hot = hovered(X, Y, W, H);
  if (hot && in_.mouseMoved) focus_ = id;
  const bool focused = focus_ == id;
  rowFrame(X, Y, W, H, focused);
  const float px = s(theme_->body);
  const float ty = Y + (H - OverlayBuilder::lineHeight(px)) * 0.5f;
  o_->textPx(X + s(16), ty, text, focused ? theme_->textBright : theme_->text, px);
  const std::string shown = listening ? (std::fmod(time_, 0.8) < 0.5 ? "PRESS A KEY..." : "") : std::string(keys);
  const float kw = std::max(s(90), OverlayBuilder::measure(shown, s(theme_->small)) + s(24));
  const float kx = X + W - kw - s(14), ky = Y + H * 0.5f - s(14);
  o_->panel(kx, ky, kw, s(28), listening ? theme_->highlight : theme_->panel,
            listening ? theme_->textBright : theme_->panelBorder, s(5), 1.0f);
  o_->textPx(kx + kw * 0.5f, ky + (s(28) - OverlayBuilder::lineHeight(s(theme_->small))) * 0.5f, shown,
             listening ? theme_->textBright : theme_->text, s(theme_->small), FontFace::Mono, TextAlign::Center);
  return (focused && in_.accept) || (hot && in_.mousePressed);
}

bool Ui::listRow(float x, float y, float w, float h, std::string_view text, std::string_view detail,
                 std::string_view sub, bool enabled) {
  const int id = nextFocus();
  const float X = s(x), Y = s(y), W = s(w), H = s(h);
  const bool hot = hovered(X, Y, W, H);
  if (hot && in_.mouseMoved) focus_ = id;
  const bool focused = focus_ == id;
  rowFrame(X, Y, W, H, focused);
  o_->line(X, Y + H, X + W, Y + H, 1.0f, theme_->textDim.withAlpha(0.3f));
  const float px = s(theme_->body);
  const OverlayColor c = !enabled ? theme_->textDim : focused ? theme_->textBright : theme_->text;
  o_->textPx(X + s(16), Y + s(8), text, c, px);
  o_->textPx(X + W - s(14), Y + s(8), detail, enabled ? theme_->text : theme_->textDim, px, FontFace::Mono,
             TextAlign::Right);
  if (!sub.empty()) o_->textPx(X + s(16), Y + s(8) + OverlayBuilder::lineHeight(px), sub, theme_->textDim, s(theme_->small));
  return enabled && ((focused && in_.accept) || (hot && in_.mousePressed));
}

int Ui::tabs(float x, float y, const std::vector<std::string>& labels, int current) {
  float pen = s(x);
  const float px = s(theme_->body), H = s(36);
  int result = current;
  for (int i = 0; i < int(labels.size()); ++i) {
    const float tw = OverlayBuilder::measure(labels[size_t(i)], px) + s(36);
    const bool hot = hovered(pen, s(y), tw, H);
    const bool sel = i == current;
    o_->panel(pen, s(y), tw, H, sel ? theme_->highlight : OverlayColor{0, 0, 0, 0},
              sel ? theme_->textBright : theme_->panelBorder.withAlpha(0.5f), s(6), 1.0f);
    o_->textPx(pen + tw * 0.5f, s(y) + (H - OverlayBuilder::lineHeight(px)) * 0.5f, labels[size_t(i)],
               sel ? theme_->textBright : theme_->textDim, px, FontFace::Mono, TextAlign::Center);
    if (hot && in_.mousePressed) result = i;
    pen += tw + s(8);
  }
  if (in_.tabNext && !labels.empty()) result = (current + 1) % int(labels.size());
  return result;
}

int Ui::scrollFor(int focusInList, int count, int visible) {
  if (count <= visible) return scroll_ = 0;
  if (in_.wheel != 0) {
    scroll_ -= int(std::round(in_.wheel)) * 2;
  } else if (focusInList >= 0 && (in_.up || in_.down)) {
    if (focusInList < scroll_) scroll_ = focusInList;
    if (focusInList >= scroll_ + visible) scroll_ = focusInList - visible + 1;
  }
  return scroll_ = std::clamp(scroll_, 0, count - visible);
}

}  // namespace fh
