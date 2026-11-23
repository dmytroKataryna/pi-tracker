// Повторення 2: час життя тимчасових об'єктів, висячі посилання і view.
//
// Частина 1 — передбачити. НЕ запускаючи, допишіть після кожного випадку:
//   OK — або — UB: що саме вже знищено і в який момент.
// Потім зберіть з ASan і запустіть кожен випадок окремо (ASan зупиняє програму
// на першій помилці):
//
//   clang++ -std=c++20 -g -fsanitize=address sandbox/repeat/r2_lifetime.cpp -o
//   /tmp/r2 for i in 1 2 3 4 5 6 7 8 9; do echo "== $i";
//   ASAN_OPTIONS=detect_stack_use_after_return=1 /tmp/r2 $i 2>&1 | grep -m1 -E
//   "ERROR|^ok"; done
//
// Частина 2 — виправити Logger (внизу файлу).

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Читає кожен байт у НАШОМУ коді (щоб ASan точно помітив) і друкує "ok: ...".
void show(std::string_view s) {
  unsigned sum = 0;
  for (char ch : s) {
    sum += static_cast<unsigned char>(ch);
  }
  std::printf("ok: %.*s (sum %u)\n", static_cast<int>(s.size()), s.data(), sum);
}

class Config {
public:
  explicit Config(std::string name) : m_droneName(std::move(name)) {}
  std::string_view droneNameView() const { return m_droneName; }
  const std::string &droneNameRef() const { return m_droneName; }

private:
  std::string m_droneName;
};

// Довгі назви — щоб рядок точно лежав у купі.
Config loadConfig() {
  return Config{"recon-quadcopter-unit-07-kharkiv-sector-north"};
}
std::string makeName() {
  return "fpv-interceptor-unit-12-zaporizhzhia-sector-south";
}

void case1() {
  std::string_view name = loadConfig().droneNameView();
  show(name);
}
// UB. droneNameView повертає string_view і це обєкт вже буде знищено вкінці ;
// 45 рядка

void case2() {
  const Config cfg = loadConfig();
  std::string_view name = cfg.droneNameView();
  show(name);
}
// ok

void case3() {
  const std::string &name = makeName();
  show(name);
}
// ok

void case4() {
  const std::string &name = loadConfig().droneNameRef();
  show(name);
}
// UB. droneNameRef поверне посилання, яке буде знищено вкінці ;

void case5() {
  std::string_view name = makeName();
  show(name);
}
// ub, бо string_view не продовжив життя

std::function<int()> makeCounter() {
  int count = 0;
  return [&count] { return ++count; };
}

void case6() {
  auto next = makeCounter();
  int value = next();
  std::printf("ok: %d\n", value);
}
// ub. int count вже буде знищено на момент виклику next()

// Частина 2: виправте Logger так, щоб він сам володів назвою.
// Конструктор має приймати і рядок у лапках, і std::move(s) — без зайвих копій
// (правила з R1).
class Logger {
public:
  explicit Logger(std::string name) : m_name(std::move(name)) {}
  void log(std::string_view message) const {
    show(m_name);
    show(message);
  }

private:
  std::string m_name;
};

void case7() {
  Logger logger{"sensord-telemetry-uplink-service-instance-01"};
  logger.log("started");
}
// ок

void case8() {
  std::string name = loadConfig().droneNameRef();
  show(name);
}
// ок

void case9() {
  std::span<const int> ids =
      std::vector<int>{101, 102, 103, 104, 105, 106, 107, 108};
  int sum = 0;
  for (int id : ids) {
    sum += id;
  }
  std::printf("ok: sum %d\n", sum);
}
// ub, бо span не продовжує життя. вже на ; все знищено

int main(int argc, char **argv) {
  const int n = argc > 1 ? std::atoi(argv[1]) : 0;
  switch (n) {
  case 1:
    case1();
    break;
  case 2:
    case2();
    break;
  case 3:
    case3();
    break;
  case 4:
    case4();
    break;
  case 5:
    case5();
    break;
  case 6:
    case6();
    break;
  case 7:
    case7();
    break;
  case 8:
    case8();
    break;
  case 9:
    case9();
    break;
  default:
    std::puts("usage: r2 <1..9>");
  }
}
