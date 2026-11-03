#pragma once
// Purple Friends over Wi-Fi: the Watcher ("Joy") meets the Purple app on the
// same network and plays with it. Same wire format as the app's
// LanFriendLink (apps/purple/lib/friends/lan_link.dart):
//   - every 2 s a UDP broadcast on port 47810:
//       {"purple":1,"name":"Joy#000000","port":47811,"client":false}
//   - the phone connects over TCP to port 47811; one JSON object per line.
//   - messages as in friends.dart: hello, ping, start, act (rock-paper-scissors
//     rounds and picks from games.dart).
// The Watcher knows the three "rounds" games (rock-paper-scissors, dice duel,
// coin flip): its hello lists them in "acts", and its token "000000" makes
// it the leader, so the phone never starts anything it doesn't know.
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <functional>
#include <string>
#include <vector>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <esp_netif.h>

namespace purple {

class FriendsLan {
 public:
  static constexpr int kDiscoveryPort = 47810, kTcpPort = 47811;
  static constexpr float kBeat = 0.9f;  // seconds, like the app

  // Hooks into the rest of the Watcher.
  std::function<void(const char *face, float seconds)> react;  // Purple face id
  std::function<void(const std::string &text, int ms)> notify;  // text at the bottom
  std::function<void(const char *sound)> sound;                 // "popup", "success"
  std::function<bool()> canPlay;                                // idle, not talking

  void start() {
    xTaskCreatePinnedToCoreWithCaps(&FriendsLan::Task, "purple_friends", 6144, this, 2, nullptr, 0,
                                    MALLOC_CAP_SPIRAM);
  }

 private:
  static constexpr const char *TAG = "PurpleFriends";
  static constexpr const char *kName = "Joy", *kToken = "000000";

  int udp_ = -1, listen_ = -1, client_ = -1;
  std::string rx_;
  double now_ = 0, lastAnnounce_ = -10, lastPing_ = 0, lastHeard_ = 0;
  std::string friend_;  // their name after hello
  double nextAuto_ = 1e18;

  // Timed steps of the current activity; dropped when the activity ends.
  struct Step {
    double at;
    int gen;
    std::function<void()> fn;
  };
  std::vector<Step> steps_;
  int gen_ = 0;

  // The rounds games (games.dart _Rounds + RockPaperScissors / DiceDuel /
  // CoinFlip): best of three, the starter starts each round.
  enum Game { kNone, kRps, kDice, kCoin };
  Game game_ = kNone, lastGame_ = kNone;
  bool playing_ = false, starter_ = false;
  int round_ = 0, winsMe_ = 0, winsThem_ = 0;
  std::string mine_;
  std::vector<std::pair<int, std::string>> theirs_;
  static constexpr int kWinsNeeded = 2, kMaxRounds = 7;

  static double Now() { return esp_timer_get_time() / 1e6; }
  static float Rand(float a, float b) { return a + (b - a) * ((rand() & 0xFFFF) / 65535.0f); }

  static void Task(void *arg) {
    static_cast<FriendsLan *>(arg)->Run();
  }

  // Wi-Fi connected with an address (lwIP isn't even started before Wi-Fi).
  static bool NetworkUp() {
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta == nullptr || !esp_netif_is_netif_up(sta)) return false;
    esp_netif_ip_info_t ip = {};
    return esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0;
  }

  void Run() {
    while (true) {
      if (!NetworkUp()) {
        if (client_ >= 0 || udp_ >= 0 || listen_ >= 0) {
          Lost();
          CloseAll();
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
      if (!OpenSockets()) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }
      Loop();
    }
  }

  bool OpenSockets() {
    CloseAll();
    udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (udp_ < 0 || listen_ < 0) return false;
    int yes = 1;
    setsockopt(udp_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    setsockopt(udp_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(kDiscoveryPort);
    if (bind(udp_, (sockaddr *)&a, sizeof(a)) < 0) return false;
    a.sin_port = htons(kTcpPort);
    if (bind(listen_, (sockaddr *)&a, sizeof(a)) < 0 || listen(listen_, 2) < 0) return false;
    ESP_LOGI(TAG, "Looking for Purples on the Wi-Fi");
    return true;
  }

  void CloseAll() {
    if (client_ >= 0) close(client_);
    if (listen_ >= 0) close(listen_);
    if (udp_ >= 0) close(udp_);
    client_ = listen_ = udp_ = -1;
  }

  void Loop() {
    while (true) {
      now_ = Now();
      if (now_ - lastAnnounce_ >= 2) {
        lastAnnounce_ = now_;
        Announce();
      }
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(udp_, &rd);
      FD_SET(listen_, &rd);
      int maxfd = udp_ > listen_ ? udp_ : listen_;
      if (client_ >= 0) {
        FD_SET(client_, &rd);
        if (client_ > maxfd) maxfd = client_;
      }
      timeval tv = {0, 50 * 1000};
      int n = select(maxfd + 1, &rd, nullptr, nullptr, &tv);
      if (n < 0 || !NetworkUp()) return;  // network went away: reopen when it's back
      if (n > 0) {
        if (FD_ISSET(udp_, &rd)) {
          char buf[256];
          recv(udp_, buf, sizeof(buf), 0);  // phones announce too; we wait for them to connect
        }
        if (FD_ISSET(listen_, &rd)) Accept();
        if (client_ >= 0 && FD_ISSET(client_, &rd)) Read();
      }
      Tick();
    }
  }

  void Announce() {
    char msg[128];
    int len = snprintf(msg, sizeof(msg), "{\"purple\":1,\"name\":\"%s#%s\",\"port\":%d,\"client\":false}", kName,
                       kToken, kTcpPort);
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(kDiscoveryPort);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(udp_, msg, len, 0, (sockaddr *)&to, sizeof(to));
  }

  void Accept() {
    int s = accept(listen_, nullptr, nullptr);
    if (s < 0) return;
    if (client_ >= 0) {  // a phone coming back on a new connection takes over
      close(client_);
      EndActivity(true);
    }
    client_ = s;
    rx_.clear();
    lastHeard_ = lastPing_ = now_;
    ESP_LOGI(TAG, "A Purple connected");
    // Our hello (theirs comes after their "lan" line).
    Send("{\"t\":\"hello\",\"name\":\"Joy\",\"token\":\"000000\",\"accent\":4288120559,"
         "\"acts\":[\"rps\",\"dice\",\"coin\"]}");
  }

  void Read() {
    char buf[512];
    int n = recv(client_, buf, sizeof(buf), 0);
    if (n <= 0) return Lost();
    rx_.append(buf, n);
    size_t nl;
    while ((nl = rx_.find('\n')) != std::string::npos) {
      std::string line = rx_.substr(0, nl);
      rx_.erase(0, nl + 1);
      if (!line.empty()) OnLine(line);
    }
    if (rx_.size() > 4096) rx_.clear();  // garbage: start over
  }

  void Send(const std::string &json) {
    if (client_ < 0) return;
    std::string line = json + "\n";
    if (send(client_, line.data(), line.size(), 0) < 0) Lost();
  }

  void SendAct(const char *kind, int round, const char *pick = nullptr) {
    char msg[128];
    if (pick)
      snprintf(msg, sizeof(msg), "{\"t\":\"act\",\"a\":\"%s\",\"k\":\"%s\",\"r\":%d,\"p\":\"%s\"}", Id(game_), kind, round,
               pick);
    else
      snprintf(msg, sizeof(msg), "{\"t\":\"act\",\"a\":\"%s\",\"k\":\"%s\",\"r\":%d}", Id(game_), kind, round);
    Send(msg);
  }

  void Lost() {
    if (client_ >= 0) close(client_);
    client_ = -1;
    bool had = !friend_.empty();
    EndActivity(true);
    friend_.clear();
    nextAuto_ = 1e18;
    if (had) {
      ESP_LOGI(TAG, "Friend gone");
      react("lonely", 2);
    }
  }

  void OnLine(const std::string &line) {
    cJSON *m = cJSON_Parse(line.c_str());
    if (!m) return;
    lastHeard_ = now_;
    const char *t = Str(m, "t");
    if (!strcmp(t, "hello")) {
      bool first = friend_.empty();
      friend_ = Str(m, "name");
      if (friend_.empty()) friend_ = "Purple";
      if (first) {
        ESP_LOGI(TAG, "Met %s", friend_.c_str());
        react("greeting", kBeat * 3);
        notify("Hi " + friend_ + "!", 3000);
        sound("popup");
        After(kBeat * 3, [this] { react("excited", kBeat * 2); }, true);
        nextAuto_ = now_ + kBeat * 8;  // the first game soon after meeting
      }
    } else if (!strcmp(t, "start")) {
      Game g = FromId(Str(m, "a"));
      if (g != kNone && !playing_) StartGame(g, false);  // busy: ours goes on (we lead)
    } else if (!strcmp(t, "act")) {
      if (playing_ && FromId(Str(m, "a")) == game_) OnRoundMessage(m);
    } else if (!strcmp(t, "mood")) {
      if (!playing_) react("curious", kBeat * 2);
    }
    cJSON_Delete(m);
  }

  static const char *Str(cJSON *m, const char *k) {
    cJSON *v = cJSON_GetObjectItem(m, k);
    return cJSON_IsString(v) ? v->valuestring : "";
  }

  // ------------------------------------------------------------ timing

  void After(float secs, std::function<void()> fn, bool loose = false) {
    steps_.push_back({now_ + secs, loose ? -1 : gen_, std::move(fn)});
  }

  void Tick() {
    // Keep-alive like the app: ping every 10 s, gone after 25 s of silence.
    if (client_ >= 0) {
      if (now_ - lastPing_ >= 10) {
        lastPing_ = now_;
        Send("{\"t\":\"ping\"}");
      }
      if (!friend_.empty() && now_ - lastHeard_ > 25) Lost();
    }
    // Due steps (copy out first: a step may add more).
    std::vector<Step> due;
    for (auto it = steps_.begin(); it != steps_.end();) {
      if (it->at <= now_) {
        if (it->gen < 0 || it->gen == gen_) due.push_back(std::move(*it));
        it = steps_.erase(it);
      } else {
        ++it;
      }
    }
    for (auto &s : due) s.fn();
    // The leader starts a game now and then (every 45-90 s), when free.
    if (!friend_.empty() && !playing_ && now_ >= nextAuto_) {
      if (canPlay()) {
        // A different game from last time.
        Game g;
        do g = (Game)(kRps + rand() % 3);
        while (g == lastGame_);
        char msg[64];
        snprintf(msg, sizeof(msg), "{\"t\":\"start\",\"a\":\"%s\"}", Id(g));
        Send(msg);
        StartGame(g, true);
      } else {
        nextAuto_ = now_ + 10;
      }
    }
  }

  // ------------------------------------------------------------ rounds games

  static const char *Id(Game g) { return g == kRps ? "rps" : (g == kDice ? "dice" : (g == kCoin ? "coin" : "")); }
  static Game FromId(const char *id) {
    if (!strcmp(id, "rps")) return kRps;
    if (!strcmp(id, "dice")) return kDice;
    if (!strcmp(id, "coin")) return kCoin;
    return kNone;
  }
  static const char *Title(Game g) {
    return g == kRps ? "Rock, paper, scissors!" : (g == kDice ? "Dice duel!" : "Heads or tails?");
  }

  // Same coin on both sides, from the round number (games.dart CoinFlip).
  static const char *Coin(int r) { return (((uint64_t)r * 2654435761ULL) >> 7) % 2 == 0 ? "Heads" : "Tails"; }

  bool Valid(const std::string &p) const {
    switch (game_) {
      case kRps: return p == "rock" || p == "paper" || p == "scissors";
      case kDice: return p.size() == 1 && p[0] >= '1' && p[0] <= '6';
      case kCoin: return p == "Heads|1" || p == "Heads|0" || p == "Tails|1" || p == "Tails|0";
      default: return false;
    }
  }

  // How a pick reads on the screen.
  std::string Pretty(const std::string &p) const {
    if (game_ == kRps) return p == "rock" ? "Rock" : (p == "paper" ? "Paper" : "Scissors");
    if (game_ == kDice) return p;
    return p.substr(0, p.find('|')) + (p.back() == '1' ? " (right)" : " (wrong)");
  }

  // 1 = a beats b, -1 = loses, 0 = tie.
  int Compare(const std::string &a, const std::string &b) const {
    if (game_ == kRps) {
      auto idx = [](const std::string &p) { return p == "rock" ? 0 : (p == "paper" ? 1 : 2); };
      int i = idx(a), j = idx(b);
      return i == j ? 0 : ((i - j + 3) % 3 == 1 ? 1 : -1);
    }
    if (game_ == kDice) return (a[0] > b[0]) - (a[0] < b[0]);
    return (a.back() > b.back()) - (a.back() < b.back());  // right guess beats wrong
  }

  void StartGame(Game g, bool starter) {
    EndActivity(true);
    game_ = lastGame_ = g;
    playing_ = true;
    starter_ = starter;
    round_ = 0;
    winsMe_ = winsThem_ = 0;
    theirs_.clear();
    ESP_LOGI(TAG, "%s with %s (%s)", Id(g), friend_.c_str(), starter ? "we start" : "they start");
    notify(Title(g), 2500);
    if (starter_) Go(1);
  }

  void Go(int r) {
    if (starter_) SendAct("round", r);
    round_ = r;
    mine_.clear();
    PlayRound(r);
  }

  void PlayRound(int r) {
    if (game_ == kRps) {
      for (int i = 0; i < 3; i++) {
        After(kBeat * i, [this, i] {
          static const char *const COUNT[] = {"3", "2", "1"};
          notify(COUNT[i], 900);
          if (i == 0) react("determined", kBeat * 3);
        });
      }
      After(kBeat * 3, [this] {
        static const char *const PICKS[] = {"rock", "paper", "scissors"};
        Picked(PICKS[rand() % 3]);
      });
    } else if (game_ == kDice) {
      react("excited", kBeat * 2);
      for (int i = 0; i < 6; i++)
        After(kBeat * 0.3f * i, [this] { notify("Rolling... " + std::to_string(1 + rand() % 6), 400); });
      After(kBeat * 2, [this] { Picked(std::to_string(1 + rand() % 6)); });
    } else {
      notify("Heads or tails?", 2200);
      react("curious", kBeat * 2);
      After(kBeat * 2.5f, [this, r] {
        std::string coin = Coin(r), guess = (rand() & 1) ? "Heads" : "Tails";
        Picked(guess + "|" + (guess == coin ? "1" : "0"));
      });
    }
  }

  void Picked(const std::string &p) {
    mine_ = p;
    SendAct("pick", round_, p.c_str());
    if (game_ == kCoin)
      notify("I say " + p.substr(0, p.find('|')) + "!", 1500);
    else if (game_ == kDice)
      notify("I rolled " + p + "!", 1500);
    else
      notify(Pretty(p) + "!", 1500);
    Maybe(round_);
  }

  void OnRoundMessage(cJSON *m) {
    cJSON *rv = cJSON_GetObjectItem(m, "r");
    int r = cJSON_IsNumber(rv) ? rv->valueint : 0;
    const char *k = Str(m, "k");
    if (!strcmp(k, "round")) {
      if (r > round_) {
        round_ = r;
        mine_.clear();
        PlayRound(r);
      }
    } else if (!strcmp(k, "pick")) {
      std::string p = Str(m, "p");
      if (!Valid(p)) return;
      theirs_.push_back({r, p});
      Maybe(r);
    }
  }

  void Maybe(int r) {
    if (mine_.empty() || r != round_) return;
    std::string theirs;
    for (auto &t : theirs_)
      if (t.first == r) theirs = t.second;
    if (theirs.empty()) return;
    int out = Compare(mine_, theirs);
    if (out > 0) winsMe_++;
    if (out < 0) winsThem_++;
    char line[128];
    if (game_ == kCoin)
      snprintf(line, sizeof(line), "%d - %d   It was %s!", winsMe_, winsThem_, Coin(r));
    else
      snprintf(line, sizeof(line), "%d - %d   %s vs %s", winsMe_, winsThem_, Pretty(mine_).c_str(), Pretty(theirs).c_str());
    notify(line, 2500);
    react(out == 0 ? "surprised" : (out > 0 ? "proud" : "pouting"), kBeat * 2);
    bool over = winsMe_ >= kWinsNeeded || winsThem_ >= kWinsNeeded;
    After(kBeat * 2.5f, [this, over, r] {
      if (over || r >= kMaxRounds) Finish();
      else if (starter_) Go(r + 1);
    });
  }

  void Finish() {
    int won = winsMe_ == winsThem_ ? 0 : (winsMe_ > winsThem_ ? 1 : -1);
    ESP_LOGI(TAG, "%s over %d-%d", Id(game_), winsMe_, winsThem_);
    notify(won > 0 ? "I win!" : (won < 0 ? friend_ + " wins!" : "A draw!"), 3000);
    react(won > 0 ? "party" : (won < 0 ? "defeated" : "happy"), kBeat * 3);
    if (won > 0) sound("success");
    After(kBeat * 3, [this] { EndActivity(false); });
  }

  void EndActivity(bool quiet) {
    gen_++;
    bool was = playing_;
    playing_ = false;
    if (was && !quiet) nextAuto_ = now_ + Rand(45, 90);
  }
};

}  // namespace purple
