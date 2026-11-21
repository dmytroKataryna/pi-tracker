Що означають -O0, -O2, -O3, -Os?
    Рівень оптимізації: наскільки сильно компілятор переробляє код, щоб він працював швидше.
    -O0 — майже без оптимізацій: кожен рядок C++ стає «чесними» інструкціями, кожна змінна лежить у пам'яті. Для налагодження.
    -O2 — вбудовує дрібні функції (inline), тримає змінні в регістрах, викидає «зайві» читання й записи, переставляє інструкції. Стандарт для релізу.
    -O3 — ще агресивніше, наприклад векторизує цикли (SIMD). Для обчислювальних задач.
    -Os — оптимізує розмір, а не швидкість. Для мікроконтролерів.
    Типова налагоджувальна збірка: -O0 -g. Типова релізна: -O2 -DNDEBUG.

Що робить -DNDEBUG?
    Вимикає всі assert. У релізі зникають перевірки, які спіймали б помилку,
    наприклад assert(has_value()) у hailort::Expected: без нього value() на помилці — просто UB.

Чому помилка проявляється лише з -O2, а з -O0 «працює»?
    Оптимізатор має право вважати, що в програмі немає UB, і переробляє код, спираючись на це.
    Якщо UB насправді є (гонка, звільнена пам'ять, вихід за межі, неініціалізована змінна), з -O0 вона може випадково працювати, а з -O2 ламається.
    Приклад (День 17): while (!stopRequested) зі звичайним bool з -O0 зупинявся, а з -O2 висів вічно:
    компілятор прочитав змінну один раз перед циклом. Виправлення — std::atomic<bool>.

Що означає код виходу 128 + N?
    Процес убив сигнал номер N.
    130 = SIGINT (Ctrl+C), 134 = SIGABRT (assert, abort, std::terminate), 137 = SIGKILL (kill -9, OOM),
    139 = SIGSEGV (звернення до чужої пам'яті).
    Ctrl+Z — це не завершення, а SIGTSTP: процес лише зупинено (стан T у ps), і він далі тримає камеру чи файли.

Програма падає з exit=139 щоразу в різних місцях і лише в релізі. Що робити?
    1. Прочитати симптом: 139 — SIGSEGV. Місце щоразу інше — це зіпсована пам'ять або гонка, а не проста логічна помилка.
    2. Подивитися всі попередження компілятора (-Wall -Wextra -Wconversion), до одного: вони часто вказують на справжню помилку.
    3. gdb на збірці -O2 -g: bt покаже, де впало. Але місце падіння ≠ місце помилки (див. нижче).
    4. Санітайзери — головний інструмент для таких випадків (див. нижче).
    5. Зменшити приклад: менше потоків, менші дані. Якщо раніше працювало — git bisect.

Що таке санітайзери і який коли брати?
    Спеціальна збірка, яка перевіряє кожне звернення до пам'яті й повідомляє про помилку з номерами рядків.
    -fsanitize=address (ASan) — звільнена пам'ять, вихід за межі буфера, витоки. Працює на Raspberry Pi 5.
    -fsanitize=undefined (UBSan) — переповнення знакового int, неправильні перетворення, невирівняний доступ. Працює на Pi 5.
    -fsanitize=thread (TSan) — гонки даних. На Pi 5 не працює (47-бітний адресний простір), на Mac з clang працює.
    Обмеження: перевіряється лише код, зібраний із санітайзером. Помилку всередині чужої бібліотеки (libgstapp) ASan може не побачити.
    Ще одна корисна збірка без санітайзерів: -D_GLIBCXX_ASSERTIONS — перевірка меж у std::vector, std::span тощо (як IndexOutOfBounds у Java).

Як локально зібрати й запустити тести так само, як CI: з -Werror і з ASan + UBSan?
    Кожна конфігурація — у своїй теці build-*, тож перемикатися між ними можна без переконфігурування
    (build*/ уже в .gitignore). Перший cmake у новій теці завантажує googletest — це нормально.

    1) Попередження = помилки (як job "test" у CI):
        cmake -S portable -B build-werror -DCMAKE_BUILD_TYPE=Debug -DCMAKE_COMPILE_WARNING_AS_ERROR=ON
        cmake --build build-werror --parallel
        ctest --test-dir build-werror --output-on-failure

    2) ASan + UBSan (як job "sanitizers" у CI):
        Mac (clang + libc++):
        cmake -S portable -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_COMPILE_WARNING_AS_ERROR=ON \
          -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG"
        Linux / Pi (GCC + libstdc++) — те саме, але останній прапорець -D_GLIBCXX_ASSERTIONS.
        cmake --build build-asan --parallel
        ctest --test-dir build-asan --output-on-failure

    Лише частина тестів (швидше, і видно весь вивід):
        ./build-asan/unit_tests --gtest_filter='FrameDecoder.*'

    Що означають параметри:
    -DCMAKE_BUILD_TYPE=Debug — -O0 -g: без оптимізацій і з номерами рядків у звітах санітайзерів і в gdb.
    -DCMAKE_COMPILE_WARNING_AS_ERROR=ON — CMake додає -Werror: будь-яке попередження зупиняє збірку.
        Навіщо: -Wconversion, -Wunused-parameter тощо часто вказують на справжню помилку, а CI однаково впаде на них.
        Наслідок: заглушка з невикористаним параметром із -Werror не скомпілюється.
    -fsanitize=address (ASan) — вихід за межі масиву, звернення до звільненої пам'яті, подвійне звільнення.
        Програма ≈2 рази повільніша і їсть більше пам'яті — лише для тестів, не для польоту.
    -fsanitize=undefined (UBSan) — UB під час виконання: переповнення знакового int, зсув на ≥ ширину типу,
        невирівняний доступ, розіменування nullptr.
    -fno-sanitize-recover=all — після першої знайденої помилки зупинити програму (код 134).
        Без нього UBSan лише друкує "runtime error" і працює далі: процес завершується з кодом 0, тест ЗЕЛЕНИЙ,
        і помилку видно тільки в лозі. Перевірено: переповнення int без цього прапорця — exit 0, з ним — 134.
    -fno-omit-frame-pointer — не викидати покажчик кадру стека, щоб стек викликів у звіті був повним і читабельним.
    -D_GLIBCXX_ASSERTIONS (libstdc++, Linux) і -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG (libc++, Mac) —
        перевірка меж у самій стандартній бібліотеці: v[i], span::subspan, front() на порожньому тощо → abort з повідомленням.
        Навіщо поруч з ASan: читання v[5] при size() == 3, але capacity() == 10 — пам'ять «своя», тож ASan на Linux
        мовчить (exit 0), а _GLIBCXX_ASSERTIONS ловить: "Assertion '__n < this->size()' failed" (перевірено на GCC 13).

    ASan і TSan в одній збірці поєднати не можна. Гонки — окремою збіркою (на Mac; на Pi 5 TSan не працює):
        cmake -S portable -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
        cmake --build build-tsan --parallel && ctest --test-dir build-tsan --output-on-failure

Приклад: помилка, яку видно лише з ASan
    std::vector<int> v(3, 7);
    const int *first = &v[0];
    v.push_back(8);              // vector переїхав у нову пам'ять, стару звільнив
    std::cout << *first << '\n'; // читання звільненої пам'яті
    Звичайна збірка (-O0 чи -O2): жодного попередження, друкує сміття (1432050758), exit=0 — помилка повністю тиха.
    З -fsanitize=address -g: "heap-use-after-free", читання — рядок 9, звільнення — рядок 8 (push_back), виділення — рядок 6.

Чому місце падіння не збігається з місцем помилки?
    Use-after-free: пам'ять звільнили в одному місці, а падає той, хто прийшов до неї пізніше.
    День 18: я віддав буфер у gst_app_src_push_buffer через get() замість release(), і GstBufferPtr звільнив його на } —
    а впав потік GStreamer "src:src", коли взяв цей буфер із черги. У bt не було жодної моєї функції.
    Тому після gdb варто думати «хто і коли звільнив цю пам'ять», а не лише «де впало».

Що станеться, якщо в operator= переміщення забути close старого дескриптора?
    Витік: старий номер лишається відкритим, хоча ним уже ніхто не користується. Щоразу +1 дескриптор.
    Процес має ліміт відкритих дескрипторів (ulimit -n, на Linux типово 1024). Коли його вичерпано, open/socket/accept
    повертають -1 з помилкою EMFILE "Too many open files" — ламається все одразу: порт, лог, UDP-сокет телеметрії.
    Перепідключення до порту раз на секунду: на 1022-й секунді (≈17 хв польоту) open повертає -1 (перевірено з ulimit -n 1024;
    номери 0, 1, 2 зайняті stdin/stdout/stderr).
    Не плутати з подвійним close: там навпаки — закриваємо чужий файл, який отримав той самий номер, і тихо пишемо не туди.

Як знайти витік файлових дескрипторів ще під час розробки?
    1. Юніт-тест класу-власника: після присвоєння чи деструктора старий номер закритий (fcntl(fd, F_GETFD) == -1, errno EBADF).
       У мене це UniqueFd.MoveAssignmentClosesOldFd (tests/unique_fd_test.cpp).
    2. Порахувати відкриті дескриптори «до» і «після» сценарію (наприклад, 100 перепідключень) — числа мають збігатися:
           std::size_t openFdCount() {
             std::size_t count = 0;
             for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator("/dev/fd")) {
               ++count;
             }
             return count;
           }
       /dev/fd є і на Linux, і на Mac (перевірено: 5 витоків → before 4, after 9).
       У програмі: раз на хвилину писати в лог "open fds: N". Число, що в довгому прогоні лише росте, — витік.
    3. Зменшити ліміт під час розробки: ulimit -n 64 перед запуском — витік проявиться за хвилину на столі, а не в польоті.
    4. Що саме протікає в запущеному процесі: lsof -p <PID> або ls -l /proc/<PID>/fd (Linux). Той самий шлях багато разів — винуватець.
    5. Де в коді його відкрили (лише Linux): valgrind --track-fds=yes ./app — при виході стек для кожного незакритого дескриптора;
       strace -e trace=openat,close ./app — openat без парного close.
    Пастка: ASan/LeakSanitizer шукає лише витоки пам'яті. Витік дескрипторів він не бачить, «ASan чистий» тут нічого не гарантує.
    Android-аналогія: StrictMode.detectLeakedClosableObjects() (CloseGuard). У C++ вбудованого аналога немає, найближче — valgrind --track-fds.
    Головний захист — дизайн: результат open/socket одразу кладемо в UniqueFd, жодного «голого» int для дескриптора.

Повторення: падіння в польоті, нестабільні падіння, недійсні посилання
(Тема 6, листопад 2026)

Програма на дроні впала в польоті: у лозі лише "Segmentation fault", на столі не відтворюється. Що налаштувати ЗАЗДАЛЕГІДЬ?
    Core dump — знімок пам'яті процесу в момент смерті: стеки всіх потоків і значення змінних.
    Android-аналогія: tombstone + mapping.txt (або нестиснуті .so) для Crashlytics.
    1) Дозволити core dump:
         ulimit -c unlimited       — для запуску з терміналу (діє на цю оболонку й процеси, запущені з неї)
         LimitCORE=infinity        — у [Service] systemd-юніта (ulimit із термінала на сервіс не діє)
       Куди піде файл, вирішує /proc/sys/kernel/core_pattern. Якщо там "|.../systemd-coredump" — дампи збирає systemd:
         coredumpctl list          — усі падіння
         coredumpctl gdb sensord   — відкрити останнє падіння sensord одразу в gdb
    2) Збирати з -g (RelWithDebInfo = -O2 -g): у бінарнику з'являється таблиця «адреса → файл:рядок, імена змінних».
    3) Зберегти ТОЙ САМИЙ незрізаний бінарник, що літав (strip прибирає -g). Без нього стек — лише адреси, як без mapping.txt.
       Нова збірка з тих самих джерел не підійде: адреси можуть зсунутися (gdb попередить "core file may not match").

Що робити з core dump після посадки?
    gdb ./sensord core             (або coredumpctl gdb sensord)
        bt                         — стек потоку, що впав; читати згори вниз до першої СВОЄЇ функції
        frame 3                    — перейти в кадр №3 зі списку bt
        info locals / print x      — локальні змінні / будь-який вираз у цьому кадрі
        info threads, thread 2, bt — інші потоки (корисно, коли підозра на гонку)
    lldb: bt, frame select 3, frame variable, p x.
    З -O2 частина змінних — "<optimized out>": компілятор тримав їх лише в регістрах. Це нормально, дивитися сусідні кадри.

Витік пам'яті чи "Segmentation fault" — як відрізнити?
    Витік закінчується НЕ SIGSEGV. Коли пам'ять вичерпано, ядро вбиває процес (OOM killer):
    сигнал SIGKILL, у терміналі "Killed", код виходу 137 (128 + 9), у журналі ядра (dmesg або journalctl -k) —
    "Out of memory: Killed process 1234 (sensord)". Core dump при цьому не пишеться: SIGKILL не перехопити.
    "Segmentation fault" — SIGSEGV, код 139: звернення за поганою адресою (nullptr, звільнена пам'ять, вихід за межі).

Тест падає приблизно раз на 50 запусків, щоразу в іншому місці. Які інструменти й у якому порядку?
    «Щоразу в іншому місці» = місце падіння ≠ місце помилки: хтось раніше зіпсував пам'ять, а падає той,
    хто скористався нею пізніше. Друга типова причина — гонка потоків.
    1) ASan + UBSan (build-asan). Вони ловлять неправильний доступ У МОМЕНТ доступу, а не пізніше, і зазвичай
       уже з першого запуску: перевіряється кожне звернення, а не лише те, що випадково зачепило чужі дані.
    2) Якщо там чисто, а потоків більше одного — TSan (build-tsan на Mac; на Pi 5 не працює).
    3) Відтворити під налагоджувачем:
         ./unit_tests --gtest_filter='X.*' --gtest_repeat=200 --gtest_break_on_failure
       --gtest_shuffle — перемішати порядок тестів: інколи заважає те, що лишилося від попереднього тесту.
    ASan і TSan в одній збірці — ні. clang: "'-fsanitize=address' not allowed with '-fsanitize=thread'":
    кожен по-своєму розмічає «тіньову» пам'ять. ASan + UBSan — можна, так і зроблено в build-asan.

Що не так?
    std::vector<Frame> frames;
    frames.push_back(a);
    Frame &first = frames[0];
    frames.push_back(b);
    log(first.id);
    Коли size() == capacity(), push_back виділяє новий, більший блок, переносить туди елементи і ЗВІЛЬНЯЄ старий.
    first вказує в старий, уже звільнений блок — це висяче посилання (dangling), а не «інший елемент».
    Kotlin: ArrayList<Frame> зберігає посилання, а самі об'єкти живуть окремо, тож val first = list[0] лишається дійсним.
    std::vector<Frame> зберігає самі Frame усередині свого блоку, а посилання в C++ нічим не володіє і нічого не тримає живим.
    Те саме з покажчиками, ітераторами, std::span і std::string_view на вміст vector.
    Звичайна збірка (перевірено на Mac): друкує id=4 замість 1, exit 0 — не падає, а мовчки дає сміття:
    звільнена пам'ять лишається в процесі, і аллокатор уже записав туди свої дані.
    ASan:
        ERROR: AddressSanitizer: heap-use-after-free
        READ of size 4
            #0 in main uaf.cpp:10                  ← де прочитали
        freed by thread T0 here:
            #8 vector::push_back
            #9 in main uaf.cpp:9                   ← хто звільнив
        previously allocated by thread T0 here:
            #9 in main uaf.cpp:7                   ← де виділили
    Три стеки — кожен читати до першого рядка свого коду.
    Виправлення: брати посилання вже після всіх push_back; зберігати індекс; скопіювати: Frame first = frames[0];
    reserve наперед — лише коли кількість точно відома, і це крихко: хтось потім додасть ще один push_back.

Методичка: core dump на Pi — увімкнути, зібрати, дістати, відкрити
(Перевірено на Pi 5: Debian, gdb 16.3, g++ 14; листопад 2026)

Що таке core dump і коли він потрібен?
    Знімок пам'яті процесу в момент падіння: стеки всіх потоків, значення змінних. Пише його ядро, на пристрої
    не потрібен ні gdb, ні інший налагоджувач. Відповідає на питання ЩО зламалося (стек, nullptr, номер кадру).
    Історії в ньому немає: локальних змінних функцій, які вже повернулися, не видно, точки зупинки й кроки не працюють.
    ЧОМУ так сталося — перевіряють потім живим запуском (точка зупинки, ASan/TSan, юніт-тест).
    Android-аналогія: tombstone / heap dump.

1) Зібрати з налагоджувальною інформацією
    g++ -std=c++20 -g -O0 app.cpp -o app      — для навчання: усі змінні видно
    g++ -std=c++20 -g -O2 app.cpp -o app      — як для польоту (CMake: -DCMAKE_BUILD_TYPE=RelWithDebInfo)
    Зберегти САМЕ ЦЕЙ бінарник поруч із версією коду. Відбиток збірки:
        readelf -n app | grep "Build ID"
    Core від однієї збірки з іншим бінарником не читається правильно (gdb попереджає "core file may not match").

2) Перевірити й увімкнути core dump
    ulimit -c                                  — 0 = вимкнено (на Pi так за замовчуванням)
    ulimit -c unlimited                        — увімкнути. Діє лише на ЦЮ оболонку і програми, запущені з неї;
                                                 нова SSH-сесія — знову 0
    cat /proc/sys/kernel/core_pattern          — куди пишеться файл. На Pi: "core" → у поточну теку процесу
    cat /proc/sys/kernel/core_uses_pid         — 1 → назва core.<PID> (наприклад core.2510)
    Тека має бути доступна процесу на запис, інакше core не з'явиться.
    Для systemd-сервісу (на дроні) ulimit із термінала не діє — у юніті:
        [Service]
        LimitCORE=infinity
        WorkingDirectory=/var/lib/sensord      ← тека з правом запису, туди й ляже core

3) Падіння
    ./app; echo "exit=$?"
        Segmentation fault (core dumped)       ← "(core dumped)" = файл записано
        exit=139                               ← 128 + 11, SIGSEGV
    Без ulimit буде просто "Segmentation fault" і жодного файлу.
    SIGKILL (OOM killer, kill -9) core НЕ пише — його не перехопити.

4) Дістати з пристрою (на Mac)
    scp pi5:coredemo/app pi5:coredemo/core.2510 ~/pi-tracker/build/coredemo/
    Розмір core ≈ пам'ять процесу: тут 800 КБ, у програми з кадрами камери — сотні МБ.
    У core може бути все, що було в пам'яті (кадри, ключі) — нікому не пересилати.

5) Відкрити
    На Pi:   gdb ./app core.2510               (gdb -tui ./app core.2510 — зверху ще й код)
    На Mac:  lldb ./app -c core.2510           (lldb читає Linux-core, пароль не потрібен)
    VS Code на Pi: Run and Debug → "core dump (gdb)" (~/pi-tracker/.vscode/launch.json: program + coreDumpPath)
    Команди gdb:
        bt                    — стек; читати згори до першої своєї функції
        frame 1  (up/down)    — перейти в інший кадр
        info args / info locals
        print *this, print m_detector, print frame.number
        list                  — код навколо поточного рядка
        quit
    Вправа 3 (sandbox/debug/ex3_model_reload.cpp), -O0:
        #0  Detector::detect (this=0x0, frame=...) at ex3_model_reload.cpp:21
        #1  Pipeline::onFrame (this=0x7ffffb985bc8, frame=...) at ex3_model_reload.cpp:40
        (gdb) frame 1
        (gdb) print *this     → {m_detector = std::unique_ptr<Detector> = {get() = 0x0}, m_targets = 99}
        (gdb) print frame.number → 100

    Пастка -O2: частина змінних "<optimized out>", а значення в пам'яті можуть бути ЗАСТАРІЛИМИ — компілятор тримав
    їх у регістрах. Та сама вправа з -O2 на Pi: print *this показав get() = 0x7ffff8a08948 і m_targets = 73728 (сміття),
    хоча насправді там nullptr і 99. Надійніше: this=0x0 у кадрі #0 і адреса падіння. Сумнівно — перезібрати з -O0.

6) Перевірити гіпотезу живим запуском
    gdb ./app → break Pipeline::reloadModel → run → next → print candidate
    VS Code: "ex3 live (gdb)", червона точка ліворуч від рядка, F5.
    Клавіші VS Code: F10 — крок (next), F11 — зайти у функцію (step), Shift+F11 — вийти (finish), F5 — далі (continue).

7) Прибрати
    rm core.*

Повторення: тема 6 (листопад 2026)

Що тут не так? Що скаже ASan? Як виправити?
    std::span<const std::uint8_t> payload = decoder.lastPayload(); // span на внутрішній буфер decoder
    decoder.feed(nextChunk);                                       // додає байти в той самий буфер
    sendToGroundStation(payload);
    span, як і string_view, нічим не володіє. feed додає байти у vector; якщо не вистачає capacity, vector виділяє
    новий блок, переносить байти і ЗВІЛЬНЯЄ старий. payload далі вказує в звільнений блок.
    ASan: НЕ "Segmentation fault", а "heap-use-after-free" з трьома стеками: читання (sendToGroundStation),
    звільнення (feed → vector::insert), виділення. Завершення від ASan: Mac — abort (134), Linux — exit 1.
    Звичайна збірка найчастіше НЕ падає, а мовчки відправляє сміття (як у вправі 1: BAD, exit 0).
    Виправлення: відправити ДО feed; або скопіювати перед feed:
        std::vector<std::uint8_t> copy(payload.begin(), payload.end());
    або lastPayload() повертає власну копію. Якщо лишаємо span — у коментарі: «дійсний до наступного feed()».

std::atomic<int> framesTotal, framesDropped; камера збільшує обидва, телеметрія рахує framesDropped / framesTotal.
Чи скаже щось TSan? Чи можна довіряти результату?
    TSan МОВЧИТЬ: кожне звернення атомарне, тож data race формально немає.
    Але довіряти не можна — це race condition (логічна гонка): між читанням framesDropped і framesTotal камера
    встигає збільшити лічильник, частка рахується з різних моментів. «TSan чистий» ≠ «код правильний».
    Для кількох значень, що мають бути узгоджені, — один м'ютекс на обидва (вправа 2, sandbox/debug/ex2_latest_position.cpp:
    atomic на кожному полі — TSan чистий, 1 269 565 змішаних позицій з 2 млн).
    Ще одна помилка без жодних інструментів: int / int — цілочисельне ділення. dropped < total → завжди 0;
    на старті total == 0 → ділення на нуль. Правильно: total == 0 ? 0.0 : static_cast<double>(dropped) / total.

Сервіс sensord зник у польоті. У журналі "Main process exited, code=killed, status=9/KILL", "core dumped" немає.
Що сталося, де підтвердити, чи можна було отримати core?
    status=9/KILL — процес убив SIGKILL (сигнал 9).
    Найімовірніше — OOM killer: скінчилася пам'ять (витік, черга без обмеження). Підтвердити в журналі ядра:
        journalctl -k   або   dmesg   → "Out of memory: Killed process 1234 (sensord)"
    Рідше: watchdog / тайм-аут зупинки systemd, чийсь kill -9.
    Core НЕ буде за жодних налаштувань: SIGKILL не перехопити, ядро core для нього не пише.
    Core вмикається не «в білді», а під час ЗАПУСКУ: LimitCORE=infinity у юніті або ulimit -c unlimited.
    -g у збірці лише робить core читабельним (імена функцій, рядки, змінні).
    SIGSEGV (139) → core є (якщо ввімкнено); SIGKILL (137) → core немає, дивитися журнал ядра.
