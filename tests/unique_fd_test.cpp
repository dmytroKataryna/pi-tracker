#include "pi_tracker/unique_fd.h"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <type_traits>
#include <utility>

using pi_tracker::UniqueFd;

// Правила класу, які перевіряє компілятор: якщо щось не так, unit_tests не збереться,
// а в помилці буде текст із лапок
static_assert(!std::is_copy_constructible_v<UniqueFd>,
              "копіювання має бути заборонене (= delete)");
static_assert(!std::is_copy_assignable_v<UniqueFd>,
              "присвоєння копією має бути заборонене (= delete)");
static_assert(std::is_nothrow_move_constructible_v<UniqueFd>,
              "конструктор переміщення має бути noexcept");
static_assert(std::is_nothrow_move_assignable_v<UniqueFd>,
              "присвоєння переміщенням має бути noexcept");
static_assert(!std::is_convertible_v<int, UniqueFd>,
              "конструктор з int має бути explicit");
static_assert(!std::is_convertible_v<UniqueFd, bool>,
              "operator bool має бути explicit");
static_assert(std::is_constructible_v<bool, UniqueFd>,
              "потрібен explicit operator bool");

namespace {

// Справжній відкритий дескриптор для тестів
int openNull() {
  const int fd = ::open("/dev/null", O_RDONLY);
  EXPECT_GE(fd, 0) << "не вдалося відкрити /dev/null";
  return fd;
}

// Чи відкритий номер у цьому процесі. Для закритого fcntl повертає -1 (EBADF)
bool isOpen(int fd) { return ::fcntl(fd, F_GETFD) != -1; }

} // namespace

TEST(UniqueFd, DefaultIsEmpty) {
  const UniqueFd fd;
  EXPECT_FALSE(fd);
  EXPECT_EQ(fd.get(), -1);
}

TEST(UniqueFd, DestructorCloses) {
  const int raw = openNull();
  {
    const UniqueFd fd{raw};
    EXPECT_TRUE(fd);
    EXPECT_EQ(fd.get(), raw);
  }
  EXPECT_FALSE(isOpen(raw)) << "деструктор не закрив дескриптор";
}

TEST(UniqueFd, MoveConstructorTransfersOwnership) {
  const int raw = openNull();
  {
    UniqueFd a{raw};
    {
      const UniqueFd b{std::move(a)};
      EXPECT_EQ(b.get(), raw);
      EXPECT_FALSE(a) << "після переміщення a має бути порожнім";
    }
    EXPECT_FALSE(isOpen(raw)) << "b (новий власник) мав закрити дескриптор";
  }
}

TEST(UniqueFd, MovedFromDoesNotClose) {
  const int raw = openNull();
  UniqueFd b;
  {
    UniqueFd a{raw};
    b = std::move(a);
  } // тут знищується a
  EXPECT_TRUE(isOpen(raw)) << "a закрив дескриптор, який уже віддав b";
  EXPECT_EQ(b.get(), raw);
}

TEST(UniqueFd, MoveAssignmentClosesOldFd) {
  const int first = openNull();
  const int second = openNull();
  UniqueFd a{first};
  UniqueFd b{second};
  b = std::move(a);
  EXPECT_FALSE(isOpen(second)) << "b не закрив свій старий дескриптор (витік)";
  EXPECT_EQ(b.get(), first);
  EXPECT_FALSE(a);
}

TEST(UniqueFd, SelfMoveAssignmentKeepsFd) {
  const int raw = openNull();
  UniqueFd fd{raw};
  UniqueFd &same = fd; // через посилання, щоб компілятор не попереджав про a = std::move(a)
  fd = std::move(same);
  EXPECT_EQ(fd.get(), raw);
  EXPECT_TRUE(isOpen(raw)) << "присвоєння самому собі закрило дескриптор";
}

TEST(UniqueFd, ReleaseGivesUpOwnership) {
  const int raw = openNull();
  int released = -1;
  {
    UniqueFd fd{raw};
    released = fd.release();
    EXPECT_FALSE(fd);
    EXPECT_EQ(fd.get(), -1);
  }
  EXPECT_EQ(released, raw);
  EXPECT_TRUE(isOpen(raw)) << "після release() деструктор не має нічого закривати";
  ::close(raw); // тепер закрити — наш обов'язок
}

TEST(UniqueFd, ResetClosesOldAndTakesNew) {
  const int first = openNull();
  const int second = openNull();
  UniqueFd fd{first};
  fd.reset(second);
  EXPECT_FALSE(isOpen(first)) << "reset(new) не закрив старий дескриптор";
  EXPECT_EQ(fd.get(), second);
  fd.reset();
  EXPECT_FALSE(isOpen(second)) << "reset() без аргументу має закрити дескриптор";
  EXPECT_FALSE(fd);
}
