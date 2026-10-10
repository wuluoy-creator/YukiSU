# ZySU

<img align="right" src="../ZySU-mini.png" width="180" alt="ZySU logosu">

ZySU, [SukiSU-Ultra](https://github.com/ShirkNeko/SukiSU-Ultra) projesinden türetilmiş, KernelSU tabanlı bir Android root çözümüdür. ARM64 çekirdek modülü, C++17 kullanıcı alanı bileşenleri ve Kotlin / Jetpack Compose ile yazılmış Manager uygulamasını içerir.

[English](../README.md) · [简体中文](../zh/README.md) · [日本語](../ja/README.md) · **Türkçe** · [Русский](../ru/README.md)

## Mevcut özellikler

- Çekirdek düzeyinde `su`, uygulama bazında yetkilendirme ve profiller, Manager imza doğrulaması, dinamik Manager ve SuperKey kimlik doğrulaması.
- Modül yükleme, etkinleştirme, devre dışı bırakma, kaldırma, açılış betikleri, WebUI ve depo yönetimi.
- ksud içine gömülü SUMHP ile OverlayFS, Magic Mount, SUMH veya bağlamama modu. `auto`, uygun olduğunda önce OverlayFS, ardından Magic Mount seçer. SUMH açıkça seçilmelidir. Harici MetaModule yaşam döngüsü arayüzleri de korunur.
- Açılış imajlarını yamalama ve geri yükleme, doğrudan LKM enjeksiyonu, bölüm işlemleri, AnyKernel3 ve ramdisk düzenleme.
- SUMH yol ve bağlama görünümü denetimleri, ADB root, su günlükleri, ortam ayarları ve tanılama.

Bu özelliklerin uygulanmış olması, her cihazın, modülün veya ortam denetleyicisinin uyumlu olduğu anlamına gelmez.

## Destek kapsamı

| Bileşen | Gereksinim |
| --- | --- |
| Mimari | ARM64 / `arm64-v8a` |
| Manager / kullanıcı alanı | Android 12 / API 31 veya üstü |
| Çekirdek | Linux 6.1 veya üstü; hazır LKM dosyaları aşağıdaki Android GKI KMI hedefleri içindir |
| Derleme modu | Yalnızca `CONFIG_KSU=m`; `CONFIG_KSU=y` desteklenmez |
| Varsayılan KMI hedefleri | `android14-6.1`, `android15-6.6`, `android16-6.12`, `android17-6.18` |

Bunlar derleme hedefleridir; doğrulanmış cihaz listesi değildir. Android sürümü tek başına KMI'yi belirlemez. Eski çekirdekler, diğer KMI'ler ve non-GKI cihazlar hazır dosyaların kapsamı dışındadır. LKM'yi boot çekirdeğine gömmek, built-in çekirdek desteği sağlamaz.

## Belgeler ve indirme

- [Kurulum](guide/installation.md): imaj seçimi, kurulum, güncelleme ve kurtarma.
- [LKM derleme ve entegrasyonu](guide/how-to-integrate.md).
- [Actions ve yerel derleme (İngilizce)](../guide/workflow-build.md).
- [ksud CLI](../ksud-cli.md), [ramdisk protokolü](../ramdisk-editor-protocol.md) ve [teknik belge dizini (Çince)](../zh/README.md).
- [Yayımlanan sürümler](https://github.com/wuluoy-creator/ZySU/releases) ve [hata bildirimi](https://github.com/wuluoy-creator/ZySU/issues).

Manager, yayımlanan resmi sürümleri denetler; CI güncelleme kanalı yoktur. Actions çıktılarından son APK olan `Manager-arm64-v8a` kullanılmalıdır. APK'yi güncellemek, açılış imajındaki çekirdek modülünü güncellemez.

Hata bildirirken cihaz ve yazılım sürümünü, `uname -r` çıktısını, KMI'yi, Manager / ksud / çekirdek sürümlerini, kurulum yöntemini, yeniden üretme adımlarını ve günlükleri ekleyin. Bilgisayarda çalışan testler cihazın açılış ve kurtarma testlerinin yerini tutmaz.

## Lisans ve teşekkürler

[kernel/LICENSE](../../kernel/LICENSE) GPL v2, kökteki [LICENSE](../../LICENSE) GPL v3 içerir. Dosya başlıkları ve üçüncü taraf lisansları da geçerlidir. WebUI paketi Apache-2.0 lisansını bildirir.

KernelSU, SukiSU-Ultra, Magisk ve [paketlenen bağımlılıkların](../../userspace/ksud/third_party/README.md) geliştiricilerine teşekkürler. Ayrıntılar için [proje özeti](../../README.md).
