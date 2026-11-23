// Повторення 1: копіювання і переміщення.
//
// Частина 1 — передбачити. НЕ запускаючи програму, допишіть після кожного кроку
// коментар: що надрукується (COPY, move, кілька рядків або нічого). Потім
// зберіть і порівняйте:
//
//   clang++ -std=c++20 -Wall -Wextra sandbox/repeat/r1_move_copy.cpp -o /tmp/r1
//   && /tmp/r1
//
// Частина 2 — написати Pipeline::push (внизу файлу).
//
// [[maybe_unused]] лише прибирає попередження «змінна не використовується», на
// копії не впливає.

#include <cstdio>
#include <deque>
#include <utility>

struct Pixels { // уявімо, що всередині 1 МБ
  Pixels() = default;
  Pixels(const Pixels &) { std::puts("   COPY"); }
  Pixels(Pixels &&) noexcept { std::puts("   move"); }
  Pixels &operator=(const Pixels &) = default;
  Pixels &operator=(Pixels &&) noexcept = default;
};

struct Frame {
  Pixels pixels;
};

Frame grab() { return Frame{}; }

Frame makeLocal() {
  Frame local;
  return local;
}

void byValue(Frame frame) { [[maybe_unused]] Frame stored = std::move(frame); }
void byConstValue(const Frame frame) {
  [[maybe_unused]] Frame stored = std::move(frame);
}
void byConstRef(const Frame &frame) {
  [[maybe_unused]] Frame stored = std::move(frame);
}
void byRvalueRef(Frame &&frame) {
  [[maybe_unused]] Frame stored = std::move(frame);
}
void byRvalueRefNoMove(Frame &&frame) { [[maybe_unused]] Frame stored = frame; }

// Частина 2. Напишіть push так, щоб для ОБОХ викликів у main
//   pipeline.push(std::move(f));
//   pipeline.push(grab());
// не надрукувалося жодного COPY.
class Pipeline {
public:
  void push(Frame frame) { m_queue.push_back(std::move(frame)); }

private:
  std::deque<Frame> m_queue;
};

int main() {
  Frame f1, f2, f3, f4, f5;

  std::puts("1) Frame f = grab();");
  [[maybe_unused]] Frame f = grab();
  // нічого

  std::puts("2) byValue(f1);");
  byValue(f1);
  // COPY move

  std::puts("3) byValue(grab());");
  byValue(grab());
  // move

  std::puts("4) byValue(std::move(f2));");
  byValue(std::move(f2));
  // move move

  std::puts("5) byConstValue(std::move(f3));");
  byConstValue(std::move(f3));
  // COPY move

  std::puts("6) byConstRef(f4);");
  byConstRef(f4);
  // COPY move

  std::puts("7) byRvalueRef(std::move(f5));");
  byRvalueRef(std::move(f5));
  // move

  std::puts("8) byRvalueRefNoMove(grab());");
  byRvalueRefNoMove(grab());
  // COPY

  std::puts("9) const Frame c = grab(); Frame g = std::move(c);");
  const Frame c = grab();
  [[maybe_unused]] Frame g = std::move(c);
  // COPY

  std::puts("10) Frame h = makeLocal();");
  [[maybe_unused]] Frame h = makeLocal();
  // 

  // Частина 2: розкоментуйте після того, як напишете push.
  std::puts("11) pipeline.push(std::move(f));");
  Pipeline pipeline;
  pipeline.push(std::move(f));
  std::puts("12) pipeline.push(grab());");
  pipeline.push(grab());
}
