#!/bin/sh
# Повторення 4: етапи збірки і «undefined reference».
#
# Три етапи, у кожного свій вхід і вихід:
#   препроцесор: .cpp + усі #include і #define      → один великий текст
#   компілятор:  цей текст                           → .o (машинний код ОДНОГО .cpp)
#   лінкер:      усі .o (і бібліотеки)                → готова програма
#
# Частина 1 — передбачити. НЕ запускаючи, допишіть під кожним випадком замість «# ?»:
#   де зупиниться збірка (препроцесор / компілятор / лінкер) або OK — і чому, одним реченням.
# Потім перевірте:  sh sandbox/repeat/r4/build.sh 1   (і так до 7)
#
# Частина 2 — після того як прогнали всі 7, подивіться в самі .o:
#   nm -C /tmp/r4/main.o | grep cellVoltage
#   nm -C /tmp/r4/battery.o | grep cellVoltage
#   nm -C /tmp/r4/battery_unsigned.o | grep cellVoltage
#   а) Що, на вашу думку, означають літери U і T?
#   б) У battery_unsigned.o теж є функція cellVoltage. Чому ж випадок 7 падає?
#   в) Чому третій рядок у випадку 4 (лінкування) не потребує -Iinclude?
#
# Mac пише «Undefined symbols for architecture arm64», Linux (Pi) — «undefined reference to».
# Це одна й та сама помилка, просто різні лінкери формулюють її по-різному.

cd "$(dirname "$0")" || exit 1
OUT=/tmp/r4
mkdir -p "$OUT"
CXX="${CXX:-clang++}"
FLAGS="-std=c++20 -Wall -Wextra"

run() {
  echo "\$ $*"
  if "$@"; then
    echo "→ OK"
  else
    echo "→ ЗУПИНИЛОСЬ (код $?)"
    return 1
  fi
}

case "$1" in
1)  # компілюємо main.cpp у .o, але забули -Iinclude
    run $CXX $FLAGS -c main.cpp -o "$OUT/main.o"
    # компілятор. Бо не знайде namespace
    ;;
2)  # те саме, тепер з -Iinclude
    run $CXX $FLAGS -Iinclude -c main.cpp -o "$OUT/main.o"
    # препроцесор. Бо у нас декілька визначених методів cellVoltage
    ;;
3)  # збираємо програму з одного main.cpp
    run $CXX $FLAGS -Iinclude main.cpp -o "$OUT/app"
    # лінкер
    ;;
4)  # кожен .cpp окремо в .o, потім лінкуємо .o разом і запускаємо
    run $CXX $FLAGS -Iinclude -c main.cpp -o "$OUT/main.o" &&
    run $CXX $FLAGS -Iinclude -c battery.cpp -o "$OUT/battery.o" &&
    run $CXX "$OUT/main.o" "$OUT/battery.o" -o "$OUT/app" &&
    run "$OUT/app"
    # лінкер
    ;;
5)  # старий код зі старим викликом
    run $CXX $FLAGS -Iinclude -c main_old.cpp -o "$OUT/main_old.o"
    # компілятор
    ;;
6)  # компілюємо файл колеги
    run $CXX $FLAGS -Iinclude -c battery_unsigned.cpp -o "$OUT/battery_unsigned.o"
    # компілятор
    ;;
7)  # збираємо програму з main.cpp і файлом колеги
    run $CXX $FLAGS -Iinclude main.cpp battery_unsigned.cpp -o "$OUT/app"
    # лінкер
    ;;
*)
    echo "usage: sh build.sh <1..7>"
    ;;
esac
