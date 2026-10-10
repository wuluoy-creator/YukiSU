# Сборка модуля ядра

ZySU работает как загружаемый модуль ARM64 `kernelsu.ko`. Поддерживаемая конфигурация — `CONFIG_KSU=m`; встраивание через `CONFIG_KSU=y` не поддерживается. [Kbuild](../../../kernel/Kbuild) отклоняет версии Linux ниже 6.1 и архитектуры, отличные от ARM64.

## Целевые ядра

Рабочие процессы DDK и локальные скрипты используют следующую матрицу:

| KMI | Серия ядра |
| --- | --- |
| `android14-6.1` | 6.1 |
| `android15-6.6` | 6.6 |
| `android16-6.12` | 6.12 |
| `android17-6.18` | 6.18 |

Это цели сборки, а не проверенные устройства. Совпадение версии ядра не гарантирует совместимость: важны KMI, конфигурация, символы и политика загрузки модулей. См. [сборку в Actions (на английском)](../../guide/workflow-build.md) и [руководство по установке](installation.md).

## Сборка с ядром вашего устройства

Нужны Linux, Make, Clang/LLVM, Git и инструментарий целевого ядра. Сначала соберите ядро с конфигурацией устройства. Сохраните сгенерированные заголовки, информацию о символах и `vmlinux` без удаления символов. Одних исходников или `modules_prepare` недостаточно.

Используйте полный репозиторий ZySU. `kernel/include/uapi` — символическая ссылка Git на `../../uapi`. Если Windows Git извлёк её как обычный текстовый файл, используйте рабочую копию с настоящими символическими ссылками в Linux/WSL.

Из корня репозитория выполните команды, заменив пути на реальные абсолютные пути:

```sh
export KDIR="/absolute/path/to/kernel/out"
export CLANG_PATH="/absolute/path/to/clang/bin"
export PATH="$CLANG_PATH:$PATH"
export ARCH=arm64
export LLVM=1
export LLVM_IAS=1
export CROSS_COMPILE=aarch64-linux-gnu-

test -f "$KDIR/vmlinux"
test -f kernel/include/uapi/supercall.h
make -C kernel CONFIG_KSU=m CONFIG_KSU_SUPERKEY=y CC=clang
llvm-strip -d kernel/kernelsu.ko
```

`KDIR` указывает на настроенное и собранное дерево ядра; при сборке с `O=out` обычно нужен каталог вывода. [Makefile](../../../kernel/Makefile) собирает внешний модуль и запускает `check_symbol` для сравнения с `vmlinux`. Результат — `kernel/kernelsu.ko`. Проверка символов не заменяет проверку загрузки и работы на устройстве.

TSR использует `CONFIG_HAVE_SYSCALL_TRACEPOINTS`. Сверьте настройки модулей, трассировки и символов с реализацией Hook. `CONFIG_KRETPROBES` включает путь kretprobe в tracepoint marker; при отключении есть резервный путь. Экспериментальный `CONFIG_KSU_KRETPROBES_SUCOMPAT` зависит от `KRETPROBES`.

## Настройки и подпись Manager

Настройки перечислены в [Kconfig](../../../kernel/Kconfig). Пример явно включает SuperKey. `CONFIG_KSU_DISABLE_MANAGER=y` отключает интеграцию Manager и SuperKey, а `CONFIG_KSU_DISABLE_POLICY=y` — индивидуальные профили приложений.

Отдельно собранный модуль не начинает автоматически доверять вашему ключу подписи APK. Kbuild принимает пару `KSU_MANAGER_CERT_SIZE` и `KSU_MANAGER_CERT_SHA256` с открытыми данными сертификата. См. [настройку подписи в Actions (на английском)](../../guide/workflow-build.md).

Версию ядра можно переопределить через `ZYSU_KERNEL_VERSION_NAME` и `ZYSU_KERNEL_VERSION_CODE`; проверка требует Python 3. Без переопределений используются данные Git, возможны запросы к GitHub. Для локальной сборки полного APK предусмотрены [build.sh](../../../scripts/build.sh) и [build.bat](../../../scripts/build.bat).
