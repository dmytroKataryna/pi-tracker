// Вправа 3 (тема 6). Конвеєр обробки кадрів: Detector рахує цілі на кожному кадрі.
// Кожні 100 кадрів конвеєр «на ходу» вантажить нову версію моделі: спершу перевіряє її
// на еталонному кадрі й лише потім ставить замість старої.
// Програма падає з "Segmentation fault". Санітайзерів немає — як у польоті.
//
// Знайдіть, ДЕ впало і ЧОМУ, лише налагоджувачем: не змінюйте код і не додавайте printf.
#include <cstdio>
#include <memory>

struct Frame {
  int number;
};

class Detector {
public:
  explicit Detector(int modelVersion) : m_modelVersion{modelVersion} {}

  int modelVersion() const { return m_modelVersion; }

  // Скільки цілей на кадрі (вигадана формула замість нейромережі)
  int detect(const Frame &frame) const { return (frame.number * 7 + m_modelVersion) % 3; }

private:
  int m_modelVersion;
};

// Перевірка нової моделі на еталонному кадрі №0: має знайти стільки цілей, скільки очікуємо
bool passesSelfTest(const Detector& candidate) {
  return candidate.detect(Frame{0}) == candidate.modelVersion() % 3;
}

class Pipeline {
public:
  explicit Pipeline(std::unique_ptr<Detector> detector) : m_detector{std::move(detector)} {}

  void onFrame(const Frame &frame) {
    if (frame.number % 100 == 0) {
      reloadModel(frame.number / 100 + 1);
    }
    m_targets += m_detector->detect(frame);
  }

  int targets() const { return m_targets; }

private:
  void reloadModel(int version) {
    auto candidate = std::make_unique<Detector>(version);
    if (!passesSelfTest(*candidate)) {
      std::printf("модель v%d не пройшла перевірку, лишаємо стару\n", version);
      return;
    }
    m_detector = std::move(candidate);
    std::printf("завантажено модель v%d\n", version);
  }

  std::unique_ptr<Detector> m_detector;
  int m_targets = 0;
};

int main() {
  Pipeline pipeline{std::make_unique<Detector>(1)};
  for (int number = 1; number <= 1000; ++number) {
    pipeline.onFrame(Frame{number});
  }
  std::printf("усього цілей: %d\n", pipeline.targets());
}
