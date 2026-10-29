// Tilepet screens: menu, lists, status pages, music player, games, gallery.
// All of them draw upright for the UI rotation (locked while open) and work
// with buttons alone or touch alone.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "PetApp.h"

namespace pet {


namespace {

struct MenuItem {
  Icon icon;
  const char *label;
};
const MenuItem MENU[9] = {
    {Icon::Feed, "FEED"},     {Icon::Play, "PLAY"},     {Icon::Clean, "CLEAN"},
    {Icon::Lights, "LIGHTS"}, {Icon::Medicine, "MEDICINE"}, {Icon::Music, "MUSIC"},
    {Icon::Status, "STATUS"}, {Icon::Settings, "SETTINGS"}, {Icon::Close, "CLOSE"},
};
constexpr int MENU_X = 9, MENU_Y = 10, CELL_W = 34, CELL_H = 31;
constexpr int ROW_Y = 13, ROW_H = 11, ROWS = 9;

void titleBar(Canvas &c, const char *title, const char *right = nullptr) {
  c.fillRect(0, 0, 120, 11, col::PANEL);
  c.text(4, 3, title, col::ACCENT);
  if (right) c.text(116 - Canvas::textWidth(right), 3, right, col::DIM_TEXT);
}

void bar(Canvas &c, int x, int y, int w, float v) {
  c.rect(x, y, w, 6, col::DIM_TEXT);
  int fill = (int)((w - 2) * clampf(v, 0, 100) / 100);
  uint16_t col = v < 25 ? col::BAD : (v < 50 ? col::WARN : col::GOOD);
  c.fillRect(x + 1, y + 1, fill, 4, col);
}

}  // namespace

// ============================================================ navigation

void PetApp::openScreen(Screen s) {
  screen_ = s;
  sel_ = 0;
  scroll_ = 0;
  page_ = 0;
  menuAnim_ = 0;
  core_.uiOpen = !(s == Screen::Pet || s == Screen::Player);
  if (s == Screen::Player) host_->musicStatus(music_);
}

void PetApp::openList(ListId id) {
  list_ = id;
  screen_ = Screen::List;
  sel_ = 0;
  scroll_ = 0;
  core_.uiOpen = true;
  if (id == ListId::Settings) aboutTaps_ = 0;
}

void PetApp::closeToPet() {
  screen_ = Screen::Pet;
  core_.uiOpen = false;
  game_ = nullptr;
}

void PetApp::menuInput(const InputEvent &e) {
  if (e.kind == InputKind::Key) {
    switch (e.key) {
      case Key::Next: sel_ = (sel_ + 1) % 9; core_.sfx.play(Sfx::Beep); break;
      case Key::Select: activateMenu(sel_); break;
      case Key::Back: core_.sfx.play(Sfx::Back); closeToPet(); break;
      default: break;
    }
    return;
  }
  if (e.kind != InputKind::Tap) return;
  float u, v;
  screenToUi(uiRot_, e.x, e.y, u, v);
  int col = (int)floorf((u - MENU_X) / CELL_W), row = (int)floorf((v - MENU_Y) / CELL_H);
  if (u >= MENU_X && v >= MENU_Y && col < 3 && row < 3) {
    sel_ = row * 3 + col;
    activateMenu(sel_);
  } else if (v > 104) {
    activateMenu(sel_);
  }
}

void PetApp::activateMenu(int item) {
  core_.sfx.play(Sfx::Select);
  switch (item) {
    case 0: openList(ListId::Feed); break;
    case 1: openList(ListId::Play); break;
    case 2: closeToPet(); brain_.clean(); break;
    case 3: closeToPet(); brain_.lights(); break;
    case 4:
      closeToPet();
      if (core_.life.sick) brain_.medicine();
      else core_.say("I'M NOT SICK");
      break;
    case 5: openList(ListId::Music); break;
    case 6: openScreen(Screen::Status); break;
    case 7: openList(ListId::Settings); break;
    default: closeToPet(); break;
  }
}

int PetApp::listCount() const {
  switch (list_) {
    case ListId::Feed: return 2;
    case ListId::Play: return gameCount();
    case ListId::Music: return 2;
    case ListId::Tracks: return music_.tracks > 0 ? music_.tracks : 0;
    case ListId::Settings: return 8;
    case ListId::Debug: return 9;
  }
  return 0;
}

const char *PetApp::listTitle() const {
  switch (list_) {
    case ListId::Feed: return "FEED";
    case ListId::Play: return "PLAY";
    case ListId::Music: return "MUSIC";
    case ListId::Tracks: return "SD PLAYER";
    case ListId::Settings: return "SETTINGS";
    case ListId::Debug: return "DEBUG";
  }
  return "";
}

void PetApp::listLabel(int i, char *label, char *value, bool &enabled) const {
  const size_t L = 32, V = 16;
  label[0] = value[0] = 0;
  enabled = true;
  const Settings &s = core_.settings;
  switch (list_) {
    case ListId::Feed:
      snprintf(label, L, i == 0 ? "MEAL" : "SNACK");
      snprintf(value, V, i == 0 ? "+40" : "+15");
      break;
    case ListId::Play:
      snprintf(label, L, "%s", game(i)->name());
      break;
    case ListId::Music:
      snprintf(label, L, i == 0 ? "SD PLAYER" : "LISTEN & DANCE");
      break;
    case ListId::Tracks: {
      const char *n = host_->musicTrackName(i);
      const char *slash = n ? strrchr(n, '/') : nullptr;
      snprintf(label, L, "%.27s", slash ? slash + 1 : (n ? n : "?"));
      if (i == music_.current) snprintf(value, V, music_.paused ? "||" : "\x02");
      break;
    }
    case ListId::Settings:
      switch (i) {
        case 0: snprintf(label, L, "VOLUME"); snprintf(value, V, "%d", host_->volume()); break;
        case 1: snprintf(label, L, "SOUND FX"); snprintf(value, V, s.sfxOn ? "ON" : "OFF"); break;
        case 2:
          snprintf(label, L, "LIFE MODE");
          snprintf(value, V, s.lifeMode == LifeMode::Gentle ? "GENTLE" : "CLASSIC");
          break;
        case 3: snprintf(label, L, "NAME"); snprintf(value, V, "%s", core_.name); break;
        case 4: snprintf(label, L, "BRIGHTNESS"); snprintf(value, V, "%d%%", s.brightness); break;
        case 5: snprintf(label, L, "AUTO-LISTEN"); snprintf(value, V, s.autoListen ? "ON" : "OFF"); break;
        case 6: snprintf(label, L, "RESET PET"); break;
        default: snprintf(label, L, "ABOUT TILEPET"); snprintf(value, V, "V1"); break;
      }
      break;
    case ListId::Debug:
      switch (i) {
        case 0: snprintf(label, L, "GALLERY"); break;
        case 1: snprintf(label, L, "IMU OVERLAY"); snprintf(value, V, core_.imuOverlay ? "ON" : "OFF"); break;
        case 2: snprintf(label, L, "BEAT OVERLAY"); snprintf(value, V, core_.beatOverlay ? "ON" : "OFF"); break;
        case 3: snprintf(label, L, "TIME SCALE"); snprintf(value, V, "X%.0f", core_.timescale); break;
        case 4: snprintf(label, L, core_.life.stage == Stage::Egg ? "HATCH NOW" : "GROW UP NOW"); break;
        case 5: snprintf(label, L, "MAKE SICK"); break;
        case 6: snprintf(label, L, "MAKE POOP"); break;
        case 7: snprintf(label, L, "NEW EGG"); break;
        default: snprintf(label, L, "SAVE NOW"); break;
      }
      break;
  }
}

void PetApp::activateList(int i) {
  if (i < 0 || i >= listCount()) return;
  core_.sfx.play(Sfx::Select);
  Settings &s = core_.settings;
  switch (list_) {
    case ListId::Feed:
      closeToPet();
      brain_.feed(i == 1);
      break;
    case ListId::Play: {
      const char *why = "";
      if (!brain_.canPlay(why)) {
        closeToPet();
        core_.sfx.play(Sfx::Refuse);
        core_.say(why);
      } else {
        startGame(i);
      }
      break;
    }
    case ListId::Music:
      if (i == 0) {
        int n = host_->musicScan();
        host_->musicStatus(music_);
        if (n > 0) openList(ListId::Tracks);
        else openScreen(Screen::Player);
      } else {
        closeToPet();
        brain_.startListen(false);
      }
      break;
    case ListId::Tracks:
      host_->musicPlay(i);
      musicWanted_ = true;
      openScreen(Screen::Player);
      break;
    case ListId::Settings:
      switch (i) {
        case 0: host_->setVolume((host_->volume() + 1) % 11); break;
        case 1: s.sfxOn = !s.sfxOn; core_.sfx.setEnabled(s.sfxOn); break;
        case 2: s.lifeMode = s.lifeMode == LifeMode::Gentle ? LifeMode::Classic : LifeMode::Gentle; break;
        case 3: nameFromSeed(host_->random32(), core_.name, NAME_LEN); break;
        case 4: s.brightness = s.brightness >= 100 ? 40 : (s.brightness >= 70 ? 100 : 70); break;
        case 5: s.autoListen = !s.autoListen; break;
        case 6: confirmStep_ = 0; openScreen(Screen::Confirm); break;
        default:
          if (++aboutTaps_ >= 5) {
            aboutTaps_ = 0;
            openList(ListId::Debug);
          }
          break;
      }
      core_.dirty = true;
      break;
    case ListId::Debug:
      switch (i) {
        case 0:
          galleryStage_ = core_.life.stage == Stage::Egg ? Stage::Adult : core_.life.stage;
          galleryRoll();
          openScreen(Screen::Gallery);
          break;
        case 1: core_.imuOverlay = !core_.imuOverlay; break;
        case 2: core_.beatOverlay = !core_.beatOverlay; break;
        case 3: core_.timescale = core_.timescale < 2 ? 60 : (core_.timescale < 100 ? 600 : 1); break;
        case 4: closeToPet(); command("pet hatch"); break;
        case 5: closeToPet(); command("pet sick"); break;
        case 6: closeToPet(); command("pet poop"); break;
        case 7: closeToPet(); newPet(false); break;
        default: writeSave(); break;
      }
      break;
  }
}

void PetApp::listInput(const InputEvent &e) {
  int n = listCount();
  auto back = [&]() {
    core_.sfx.play(Sfx::Back);
    if (list_ == ListId::Tracks) openList(ListId::Music);
    else if (list_ == ListId::Debug) openList(ListId::Settings);
    else {
      int item = list_ == ListId::Feed ? 0 : list_ == ListId::Play ? 1 : list_ == ListId::Music ? 5 : 7;
      openScreen(Screen::Menu);
      sel_ = item;
    }
  };
  if (e.kind == InputKind::Key) {
    switch (e.key) {
      case Key::Next:
        if (n) sel_ = (sel_ + 1) % n;
        core_.sfx.play(Sfx::Beep);
        break;
      case Key::Select: activateList(sel_); break;
      case Key::Back: back(); break;
      default: break;
    }
  } else if (e.kind == InputKind::Tap) {
    float u, v;
    screenToUi(uiRot_, e.x, e.y, u, v);
    if (v < ROW_Y - 1) {
      back();
      return;
    }
    int row = (int)((v - (ROW_Y - 1)) / ROW_H) + scroll_;
    if (row >= 0 && row < n) {
      sel_ = row;
      activateList(row);
    }
  } else if (e.kind == InputKind::Swipe) {
    float su, sv, u, v;
    screenToUi(uiRot_, e.startX, e.startY, su, sv);
    screenToUi(uiRot_, e.x, e.y, u, v);
    scroll_ += v < sv ? 3 : -3;
  }
  if (screen_ == Screen::List) {
    n = listCount();
    if (sel_ >= n) sel_ = n ? n - 1 : 0;
    if (sel_ < scroll_) scroll_ = sel_;
    if (sel_ >= scroll_ + ROWS) scroll_ = sel_ - ROWS + 1;
    scroll_ = clampi(scroll_, 0, n > ROWS ? n - ROWS : 0);
  }
}

void PetApp::statusInput(const InputEvent &e) {
  if (e.kind == InputKind::Key) {
    if (e.key == Key::Back) {
      core_.sfx.play(Sfx::Back);
      closeToPet();
    } else {
      page_ = (page_ + 1) % 4;
      core_.sfx.play(Sfx::Beep);
    }
  } else if (e.kind == InputKind::Tap) {
    float u, v;
    screenToUi(uiRot_, e.x, e.y, u, v);
    if (v < 12) closeToPet();
    else page_ = (page_ + 1) % 4;
  }
}

void PetApp::startGame(int i) {
  game_ = game(i);
  if (!game_) return;
  gameRot_ = uiRot_;  // locked for the round
  game_->begin(core_);
  screen_ = Screen::Game;
  core_.uiOpen = true;
}

void PetApp::gameInput(const InputEvent &e) {
  if (screen_ == Screen::Result) {
    if (e.kind == InputKind::Key || e.kind == InputKind::Tap) closeToPet();
    return;
  }
  if (!game_) return;
  if (e.kind == InputKind::Key) {
    game_->onKey(core_, e.key);
  } else if (e.kind == InputKind::Tap) {
    float u, v;
    screenToUi(gameRot_, e.x, e.y, u, v);
    game_->onTap(core_, u, v);
  }
}

void PetApp::galleryRoll() {
  if (!gallery_) gallery_ = new SpriteSet[GALLERY];
  for (int i = 0; i < GALLERY; i++) {
    galleryGenomes_[i] = decodeGenome(host_->random32());
    const Genome &g = galleryGenomes_[i];
    Build b = g.weightSet < 0.33f ? Build::Slim : (g.weightSet > 0.66f ? Build::Chubby : Build::Normal);
    gallery_[i].bake(g, galleryStage_, b, Demeanour::Plain);
  }
  galleryT_ = 0;
}

void PetApp::galleryClose() {
  delete[] gallery_;
  gallery_ = nullptr;
}

void PetApp::galleryInput(const InputEvent &e) {
  bool stepStage = false, reroll = false, leave = false;
  if (e.kind == InputKind::Key) {
    reroll = e.key == Key::Next;
    stepStage = e.key == Key::Select;
    leave = e.key == Key::Back;
  } else if (e.kind == InputKind::Tap) {
    if (e.y < 40) leave = true;
    else if (e.x < 60) reroll = true;
    else stepStage = true;
  }
  if (leave) {
    galleryClose();
    openList(ListId::Debug);
    return;
  }
  if (stepStage) {
    galleryStage_ = (Stage)(((int)galleryStage_ + 1) % (int)Stage::Count);
    for (int i = 0; i < GALLERY; i++) {
      const Genome &g = galleryGenomes_[i];
      Build b = g.weightSet < 0.33f ? Build::Slim : (g.weightSet > 0.66f ? Build::Chubby : Build::Normal);
      gallery_[i].bake(g, galleryStage_, b, Demeanour::Plain);
    }
    galleryT_ = 0;
  }
  if (reroll) galleryRoll();
}

void PetApp::confirmInput(const InputEvent &e) {
  bool yes = false, no = false;
  if (e.kind == InputKind::Key) {
    yes = e.key == Key::Select;
    no = e.key == Key::Back || e.key == Key::Next;
  } else if (e.kind == InputKind::Tap) {
    float u, v;
    screenToUi(uiRot_, e.x, e.y, u, v);
    if (v > 60) {
      yes = u < 60;
      no = !yes;
    }
  }
  if (no) {
    openList(ListId::Settings);
    sel_ = 6;
  } else if (yes && ++confirmStep_ >= 2) {
    core_.memorialCount = 0;
    newPet(false);
    closeToPet();
  }
}

// ============================================================ drawing

void PetApp::clockText(char *out, size_t n) {
  int64_t now;
  out[0] = 0;
  if (host_->wallClock(now)) {
    int m = (int)((now % 86400) / 60);
    snprintf(out, n, "%02d:%02d", m / 60, m % 60);
  }
}

void PetApp::render(Canvas &c) {
  float dt = frameDt_;
  frameDt_ = 0;
  c.noClip();
  c.setRot(0);
  switch (screen_) {
    case Screen::Game:
    case Screen::Result:
      if (game_) {
        c.setRot(gameRot_);
        game_->render(c, core_, gameRot_);
        if (screen_ == Screen::Result) drawResult(c);
      }
      c.setRot(0);
      return;
    case Screen::Gallery:
      drawGallery(c);
      return;
    case Screen::Charge:
      drawCharge(c);
      return;
    default:
      break;
  }
  scene_.drawWorld(c, core_, brain_, dt);
  char clock[8];
  clockText(clock, sizeof(clock));
  switch (screen_) {
    case Screen::Pet:
      scene_.drawHud(c, core_, brain_, uiRot_, statusBar_, battery_, charging_, clock);
      if (brain_.seq() == Seq::Farewell) drawFarewell(c);
      break;
    case Screen::Player:
      scene_.drawHud(c, core_, brain_, uiRot_, false, battery_, charging_, clock);
      drawPlayerOverlay(c);
      break;
    case Screen::Menu: drawMenu(c); break;
    case Screen::List: drawList(c); break;
    case Screen::Status: drawStatus(c); break;
    case Screen::Confirm: drawConfirm(c); break;
    default: break;
  }
  c.setRot(0);
}

void PetApp::drawMenu(Canvas &c) {
  c.setRot(uiRot_);
  c.shade(0, 0, 120, 120, 0.5f);
  int off = (int)((1 - menuAnim_) * -110);
  c.fillRect(4, 4 + off, 112, 112, col::PANEL_DARK);
  c.rect(4, 4 + off, 112, 112, col::ACCENT);
  for (int i = 0; i < 9; i++) {
    int x = MENU_X + (i % 3) * CELL_W, y = MENU_Y + (i / 3) * CELL_H + off;
    bool disabled = i == 4 && !core_.life.sick;
    if (i == sel_) {
      c.fillRect(x, y, CELL_W - 2, CELL_H - 2, col::PANEL);
      c.rect(x, y, CELL_W - 2, CELL_H - 2, col::ACCENT);
    }
    int w, h;
    iconSize(MENU[i].icon, w, h);
    drawIcon(c, MENU[i].icon, x + (CELL_W - 2 - w * 2) / 2, y + (CELL_H - 2 - h * 2) / 2, 2);
    if (disabled) c.shade(x, y, CELL_W - 2, CELL_H - 2, 0.4f);
  }
  c.textCentered(60, 106 + off, MENU[sel_].label, col::TEXT);
}

void PetApp::drawList(Canvas &c) {
  c.setRot(uiRot_);
  c.fillRect(0, 0, 120, 120, col::PANEL_DARK);
  int n = listCount();
  char right[8] = {0};
  if (n > ROWS) snprintf(right, sizeof(right), "%d/%d", sel_ + 1, n);
  titleBar(c, listTitle(), right[0] ? right : nullptr);
  if (n == 0) {
    c.textCentered(60, 50, list_ == ListId::Tracks ? "NO TRACKS" : "EMPTY", col::DIM_TEXT);
    return;
  }
  for (int r = 0; r < ROWS && scroll_ + r < n; r++) {
    int i = scroll_ + r;
    int y = ROW_Y + r * ROW_H;
    char label[32], value[16];
    bool enabled;
    listLabel(i, label, value, enabled);
    if (i == sel_) {
      c.fillRect(1, y - 2, 118, ROW_H - 1, col::PANEL);
      c.text(3, y, ">", col::ACCENT);
    }
    int vw = value[0] ? Canvas::textWidth(value) + 4 : 0;
    // keep long labels clear of the value
    int maxChars = (114 - 8 - vw) / 4;
    if ((int)strlen(label) > maxChars && maxChars > 0) label[maxChars] = 0;
    c.text(8, y, label, enabled ? col::TEXT : col::DIM_TEXT);
    if (value[0]) c.text(116 - Canvas::textWidth(value), y, value, col::ACCENT);
  }
  if (scroll_ > 0) c.text(112, 13, "\x06", col::DIM_TEXT);
  if (scroll_ + ROWS < n) c.text(112, 112, "\x07", col::DIM_TEXT);
}

void PetApp::drawStatus(Canvas &c) {
  c.setRot(uiRot_);
  c.fillRect(0, 0, 120, 120, col::PANEL_DARK);
  static const char *titles[] = {"STATS", "ABOUT", "CARE", "MEMORIAL"};
  char pg[8];
  snprintf(pg, sizeof(pg), "%d/4", page_ + 1);
  titleBar(c, titles[page_], pg);
  const LifeState &L = core_.life;
  char b[40];
  switch (page_) {
    case 0: {
      const char *names[] = {"HUNGER", "HAPPY", "ENERGY", "CLEAN", "HEALTH"};
      const float vals[] = {L.hunger, L.happy, L.energy, L.clean, L.health};
      for (int i = 0; i < 5; i++) {
        int y = 16 + i * 13;
        c.text(4, y, names[i], col::TEXT);
        bar(c, 44, y - 1, 72, vals[i]);
      }
      int y = 84;
      if (L.sick) { c.text(4, y, "SICK! NEEDS MEDICINE", col::BAD); y += 8; }
      int poops = core_.world.countProps(PropKind::Poop);
      if (poops) { snprintf(b, sizeof(b), "POOPS IN ROOM: %d", poops); c.text(4, y, b, col::WARN); y += 8; }
      if (L.callH >= 0) { c.text(4, y, "NEEDS ATTENTION!", col::WARN); y += 8; }
      if (brain_.asleep()) c.text(4, y, "SLEEPING...", col::DIM_TEXT);
      break;
    }
    case 1: {
      c.text(4, 15, core_.name, col::ACCENT, 2);
      int y = 30;
      snprintf(b, sizeof(b), "%s", stageName(L.stage));
      for (char *p = b; *p; p++) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
      c.text(4, y, b, col::TEXT);
      y += 8;
      snprintf(b, sizeof(b), "AGE %.1f DAYS", L.ageH / 24);
      c.text(4, y, b, col::TEXT);
      y += 8;
      snprintf(b, sizeof(b), "WEIGHT %.0fG", L.weight);
      c.text(4, y, b, col::TEXT);
      y += 8;
      if (L.stage >= Stage::Adult) {
        snprintf(b, sizeof(b), "FORM %s", demeanourName(L.demeanour));
        c.text(4, y, b, col::TEXT);
        y += 8;
      }
      snprintf(b, sizeof(b), "GEN %u", core_.generation);
      c.text(4, y, b, col::TEXT);
      int stars = rarity(core_.genome);
      for (int i = 0; i < stars; i++) c.text(40 + i * 5, y, "\x03", col::ACCENT);
      y += 10;
      char words[48];
      personalityWords(core_.genome, words, sizeof(words));
      // wrap at 28 characters
      const char *p = words;
      while (*p && y < 112) {
        char line[30];
        int len = (int)strlen(p);
        int cut = len > 28 ? 28 : len;
        if (len > 28) {
          while (cut > 0 && p[cut] != ' ') cut--;
          if (cut == 0) cut = 28;
        }
        memcpy(line, p, cut);
        line[cut] = 0;
        c.text(4, y, line, col::DIM_TEXT);
        y += 8;
        p += cut;
        while (*p == ' ') p++;
      }
      // the pet itself, top right
      if (core_.sprites.ready()) {
        float x, yy;
        uiToScreen(uiRot_, 92, 46, x, yy);
        FaceState f;
        f.expr = brain_.asleep() ? Expr::Asleep : Expr::Happy;
        PaletteMods m;
        Scene::drawPetAt(c, core_.sprites, core_.genome, L.demeanour,
                         core_.sprites.stage() == Stage::Egg ? Anim::EggIdle : Anim::Idle, 0, f, m, (int)x, (int)yy,
                         uiRot_, false);
      }
      break;
    }
    case 2: {
      int y = 16;
      snprintf(b, sizeof(b), "CARE MISTAKES %u", L.careMistakes);
      c.text(4, y, b, L.careMistakes > 5 ? col::WARN : col::TEXT);
      y += 10;
      snprintf(b, sizeof(b), "MEALS %lu", (unsigned long)L.feeds);
      c.text(4, y, b, col::TEXT);
      y += 8;
      snprintf(b, sizeof(b), "SNACKS %lu", (unsigned long)L.snacks);
      c.text(4, y, b, col::TEXT);
      y += 8;
      snprintf(b, sizeof(b), "GAMES %lu", (unsigned long)L.games);
      c.text(4, y, b, col::TEXT);
      y += 8;
      snprintf(b, sizeof(b), "DANCED %.0f MIN", L.danceH * 60);
      c.text(4, y, b, col::TEXT);
      y += 10;
      snprintf(b, sizeof(b), "MODE %s", core_.settings.lifeMode == LifeMode::Gentle ? "GENTLE" : "CLASSIC");
      c.text(4, y, b, col::DIM_TEXT);
      y += 8;
      if (core_.parentSeed) {
        snprintf(b, sizeof(b), "PARENT %08lX", (unsigned long)core_.parentSeed);
        c.text(4, y, b, col::DIM_TEXT);
      }
      break;
    }
    default: {
      if (!core_.memorialCount) {
        c.textCentered(60, 50, "NO ONE YET", col::DIM_TEXT);
        break;
      }
      for (int i = 0; i < core_.memorialCount; i++) {
        const MemorialEntry &m = core_.memorial[i];
        const char *how = m.how == Farewell::Stars ? "\x03" : (m.how == Farewell::RanAway ? "RAN" : "RIP");
        snprintf(b, sizeof(b), "%.10s %.1fD G%u", m.name, m.ageDays10 / 10.0f, m.generation);
        c.text(4, 16 + i * 18, b, col::TEXT);
        snprintf(b, sizeof(b), "%s %s %s", stageName(m.stage), demeanourName(m.demeanour), how);
        for (char *p = b; *p; p++) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
        c.text(8, 24 + i * 18, b, col::DIM_TEXT);
      }
      break;
    }
  }
}

void PetApp::drawPlayerOverlay(Canvas &c) {
  c.setRot(uiRot_);
  c.shade(0, 0, 120, 11, 0.3f);
  if (!music_.card || music_.tracks <= 0) {
    c.fillRect(6, 36, 108, 34, col::PANEL_DARK);
    c.rect(6, 36, 108, 34, col::ACCENT);
    c.textCentered(60, 40, "NO MUSIC", col::ACCENT);
    c.textCentered(60, 50, "PUT MP3S IN /MUSIC", col::TEXT);
    c.textCentered(60, 58, "ON THE SD CARD", col::TEXT);
    return;
  }
  const char *title = music_.title[0] ? music_.title
                                      : (music_.current >= 0 ? host_->musicTrackName(music_.current) : "");
  const char *slash = strrchr(title, '/');
  if (slash) title = slash + 1;
  c.text(3, 3, music_.paused || music_.current < 0 ? "||" : "\x02", col::ACCENT);
  int w = Canvas::textWidth(title);
  c.setClip(0, 0, 120, 120);
  if (w <= 104) {
    c.text(12, 3, title, col::TEXT);
  } else {
    tickerX_ = (tickerX_ + 1) % (w + 30);
    // clip to the ticker band in UI space: draw twice for wrap-around
    for (int k = 0; k < 2; k++) {
      int x = 12 - tickerX_ + k * (w + 30);
      if (x < 120 && x + w > 12) c.text(x, 3, title, col::TEXT);
    }
    c.fillRect(0, 0, 11, 11, col::PANEL_DARK);
    c.text(3, 3, music_.paused ? "||" : "\x02", col::ACCENT);
  }
  c.shade(0, 111, 120, 9, 0.3f);
  c.textCentered(60, 113, music_.current < 0 ? "SELECT TO PLAY" : "NEXT  PAUSE  BACK", col::DIM_TEXT);
}

void PetApp::drawResult(Canvas &c) {
  c.fillRect(10, 38, 100, 44, col::PANEL_DARK);
  c.rect(10, 38, 100, 44, col::ACCENT);
  c.textCentered(60, 44, game_->won() ? "YOU WIN!" : "GOOD TRY!", game_->won() ? col::GOOD : col::WARN, 2);
  c.textCentered(60, 60, game_->resultText(), col::TEXT);
  c.textCentered(60, 70, "\x01 HAPPIER", col::ACCENT);
}

void PetApp::drawGallery(Canvas &c) {
  c.setRot(0);
  c.clear(hex565(0xF2E6C8));
  if (!gallery_) return;
  // Cycle every animation, then turn all of them a quarter.
  int perAnim = 2;
  int cycle = (int)(galleryT_ / perAnim);
  int animCount = galleryStage_ == Stage::Egg ? 3 : ANIM_COUNT - 3;
  Anim a = galleryStage_ == Stage::Egg ? (Anim)((int)Anim::EggIdle + cycle % 3) : (Anim)(cycle % animCount);
  uint8_t rot = (uint8_t)((cycle / animCount) % 4);
  for (int i = 0; i < GALLERY; i++) {
    int cx = 20 + (i % 3) * 40, cy = 20 + (i / 3) * 40;
    c.setClip(cx - 20, cy - 20, 40, 40);
    if ((i + (i / 3)) % 2) c.fillRect(cx - 20, cy - 20, 40, 40, hex565(0xE6D6B0));
    FaceState f;
    f.blink = ((int)(galleryT_ * 2) + i) % 7 == 0;
    PaletteMods m;
    m.hue = galleryT_ * 60;
    int frame = (int)(galleryT_ * animFps(a)) % animFrames(a);
    Scene::drawPetAt(c, gallery_[i], galleryGenomes_[i], Demeanour::Plain, a, frame, f, m, cx, cy, rot, false);
    if (rarity(galleryGenomes_[i])) c.text(cx - 18, cy - 18, "\x03", col::ACCENT);
  }
  c.noClip();
  c.shade(0, 112, 120, 8, 0.5f);
  char b[32];
  snprintf(b, sizeof(b), "%s %s R%d", stageName(galleryStage_), animName(a), rot);
  for (char *p = b; *p; p++) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
  c.text(2, 113, b, col::TEXT);
}

void PetApp::drawConfirm(Canvas &c) {
  c.setRot(uiRot_);
  c.shade(0, 0, 120, 120, 0.4f);
  c.fillRect(8, 24, 104, 72, col::PANEL_DARK);
  c.rect(8, 24, 104, 72, col::BAD);
  c.textCentered(60, 32, confirmStep_ == 0 ? "RESET PET?" : "REALLY? IT'S", col::TEXT, confirmStep_ == 0 ? 2 : 1);
  c.textCentered(60, 46, confirmStep_ == 0 ? "A NEW EGG STARTS" : "GONE FOREVER", col::DIM_TEXT);
  c.fillRect(14, 64, 42, 24, col::BAD);
  c.textCentered(35, 73, "YES", col::TEXT);
  c.fillRect(64, 64, 42, 24, col::PANEL);
  c.textCentered(85, 73, "NO", col::TEXT);
}

void PetApp::drawCharge(Canvas &c) {
  c.setRot(uiRot_);
  c.clear(col::INK);
  c.rect(40, 40, 36, 18, col::BAD);
  c.fillRect(76, 45, 3, 8, col::BAD);
  c.fillRect(42, 42, 3, 14, col::BAD);
  c.textCentered(60, 68, "CHARGE ME", col::TEXT, 2);
  c.textCentered(60, 86, "ZZZ...", col::DIM_TEXT);
}

void PetApp::drawFarewell(Canvas &c) {
  c.setRot(uiRot_);
  char b[32];
  switch (brain_.farewellKind()) {
    case Farewell::Stars: snprintf(b, sizeof(b), "GOODBYE, %s", core_.name); break;
    case Farewell::RanAway: snprintf(b, sizeof(b), "%s RAN AWAY...", core_.name); break;
    default: snprintf(b, sizeof(b), "R.I.P. %s", core_.name); break;
  }
  for (char *p = b; *p; p++) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
  c.shade(0, 14, 120, 22, 0.5f);
  c.textCentered(60, 18, b, col::TEXT);
  c.textCentered(60, 27,
                 brain_.farewellKind() == Farewell::Stars ? "BACK TO THE STARS"
                 : brain_.farewellKind() == Farewell::RanAway ? "IT LEFT A NOTE AND AN EGG"
                                                             : "A NEW EGG WILL COME",
                 col::DIM_TEXT);
  if (brain_.farewellDone()) c.textCentered(60, 100, "A NEW EGG...", col::ACCENT);
}

}  // namespace pet
