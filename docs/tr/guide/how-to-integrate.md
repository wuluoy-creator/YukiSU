# Çekirdek modülünü derleme

ZySU, ARM64 için yüklenebilir `kernelsu.ko` modülü olarak çalışır. Desteklenen yapılandırma `CONFIG_KSU=m` değeridir; `CONFIG_KSU=y` ile çekirdeğe gömme desteklenmez. [Kbuild](../../../kernel/Kbuild), Linux 6.1 öncesini ve ARM64 dışındaki hedefleri reddeder.

## Derleme hedefleri

DDK iş akışları ve yerel betikler aşağıdaki hedefleri kullanır:

| KMI | Çekirdek serisi |
| --- | --- |
| `android14-6.1` | 6.1 |
| `android15-6.6` | 6.6 |
| `android16-6.12` | 6.12 |
| `android17-6.18` | 6.18 |

Bu tablo derleme hedeflerini gösterir; doğrulanmış cihaz listesi değildir. Aynı çekirdek sürümü tek başına uyumluluk sağlamaz. KMI, yapılandırma, semboller ve modül yükleme ilkesi de önemlidir. [Actions kılavuzuna (İngilizce)](../../guide/workflow-build.md) ve [kurulum kılavuzuna](installation.md) bakın.

## Cihazınızın çekirdeğiyle derleme

Linux, Make, Clang/LLVM, Git ve hedef çekirdeğin araç zinciri gerekir. Önce çekirdeği cihazın yapılandırmasıyla derleyin; oluşturulan başlıkları, sembol bilgisini ve sembolleri temizlenmemiş `vmlinux` dosyasını saklayın. Yalnızca kaynak kod veya `modules_prepare` yeterli değildir.

ZySU deposunun tamamını kullanın. `kernel/include/uapi`, `../../uapi` konumuna giden bir Git sembolik bağlantısıdır. Windows Git bunu düz metin dosyası olarak çıkardıysa, Linux/WSL ortamında gerçek bağlantıları koruyan bir çalışma kopyası kullanın.

Depo kökünde, örnek yolları gerçek mutlak yollarla değiştirerek çalıştırın:

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

`KDIR`, yapılandırılmış ve derlenmiş çekirdek dizinini gösterir. `O=out` kullandıysanız genellikle çıktı dizinini seçin. [Makefile](../../../kernel/Makefile), harici modülü derler ve `check_symbol` ile `vmlinux` karşılaştırması yapar. Çıktı `kernel/kernelsu.ko` dosyasıdır. Sembol denetimi cihazda yükleme, açılış veya çalışma doğrulamasının yerine geçmez.

TSR, `CONFIG_HAVE_SYSCALL_TRACEPOINTS` kullanır. Modül, izleme ve sembol ayarlarını Hook uygulamasıyla karşılaştırın. `CONFIG_KRETPROBES`, tracepoint marker içindeki kretprobe yolunu etkinleştirir; kapalı olduğunda alternatif yol vardır. Deneysel `CONFIG_KSU_KRETPROBES_SUCOMPAT`, `KRETPROBES` gerektirir.

## Seçenekler ve Manager imzası

Seçenekler [Kconfig](../../../kernel/Kconfig) içinde tanımlanır. Örnek, SuperKey'i açıkça etkinleştirir. `CONFIG_KSU_DISABLE_MANAGER=y`, Manager entegrasyonunu ve SuperKey'i; `CONFIG_KSU_DISABLE_POLICY=y` ise uygulamaya özel profilleri devre dışı bırakır.

Ayrı derlenen modül, özel APK imza anahtarınıza otomatik olarak güvenmez. Kbuild, açık sertifika bilgisi için `KSU_MANAGER_CERT_SIZE` ve `KSU_MANAGER_CERT_SHA256` değerlerini birlikte kabul eder. [Actions imza kılavuzuna (İngilizce)](../../guide/workflow-build.md) bakın.

Çekirdek sürümünü değiştirmek için `ZYSU_KERNEL_VERSION_NAME` ve `ZYSU_KERNEL_VERSION_CODE` kullanılır; doğrulama Python 3 gerektirir. Değer verilmezse Git bilgisi kullanılır ve GitHub sorguları yapılabilir. Tam APK'nın yerel derleme girişleri [build.sh](../../../scripts/build.sh) ve [build.bat](../../../scripts/build.bat) dosyalarıdır.
