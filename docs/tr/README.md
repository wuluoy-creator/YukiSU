# YukiSU

<img align='right' src='../YukiSU-mini.svg' width='220px' alt="yukisu logo">

[English](../README.md) | [简体中文](../zh/README.md) | [日本語](../ja/README.md) | **Türkçe** | [Русский](../ru/README.md)

Çekirdek tabanlı bir Android root çözümüdür. [`SukiSU-Ultra`](https://github.com/ShirkNeko/SukiSU-Ultra) projesinden fork edilmiştir; bazı gereksiz kısımlar kaldırılmış ve bazı ilginç değişiklikler eklenmiştir.

> **⚠️ Önemli Uyarı**
>
> YukiSU userspace **tamamen C++ ile yeniden yazılmıştır** (önceden Rust tabanlıydı). Bu, YukiSU'nun davranışının diğer KernelSU fork'larından farklı olabileceği anlamına gelir. Herhangi bir sorunla karşılaşırsanız, lütfen upstream projelere değil bize bildirin.

[![En son kararlı sürüm](https://img.shields.io/github/v/release/Anatdx/YukiSU?label=En%20son%20kararlı%20sürüm&logo=github)](https://github.com/Anatdx/YukiSU/releases/latest)
[![En son test sürümü](https://img.shields.io/badge/En%20son%20test%20sürümü-nightly.link-39C5BB.svg?logo=github)](https://nightly.link/Anatdx/YukiSU/workflows/build-manager/main)
[![Grup](https://img.shields.io/badge/Grup-Telegram-blue.svg?logo=telegram)](https://t.me/manosaba)
[![Lisans: GPL v2](https://img.shields.io/badge/Lisans-GPL%20v2-FFA500.svg?logo=gnu)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Lisans: GPL v3](https://img.shields.io/badge/Lisans-GPL%20v3-FFE211.svg?logo=gnu)](https://www.gnu.org/licenses/gpl-3.0.en.html)

## Özellikler

1. Çekirdek tabanlı `su` ve root yetkisi yönetimi
2. Harici MetaModule yaşam döngüsü ve betik entegrasyonu desteği; modül bağlama backend'ini kullanıcı kendisi seçebilir
3. [App Profile](https://kernelsu.org/zh_CN/guide/app-profile.html) ve uygulama bazında kontrol edilen non-root yapılandırmaları
4. Dinamik yönetici desteği; yerleşik paket adı/imza yolunun dışında güvenilir yöneticiler yapılandırılabilir
5. APatch tarzı SuperKey kimlik doğrulaması; hem derleme zamanında belirlenen anahtarları hem de `ksud` tarafından LKM'ye enjekte edilen anahtarları destekler
6. ADB root, sulog, SELinux hide, modül `init.rc` enjeksiyonu ve diğer upstream özellikleri
7. Yerleşik YukiZygisk; çekirdek tabanlı bir Zygisk uygulamasıdır ve [Zygisk Next](https://github.com/Dr-TSNG/ZygiskNext) modülleriyle tamamen uyumludur
8. TSR tabanlı sucompat/syscall hook altyapısı
9. Keşfetmenizi bekleyen daha fazla özellik…

## Uyumluluk Durumu

- YukiSU şu anda yalnızca yüklenebilir çekirdek modülünü (`CONFIG_KSU=m`) destekler ve artık yerleşik `CONFIG_KSU=y` seçeneğini desteklemez.
- YukiSU, Android GKI 2.0 cihazlarında (çekirdek 6.1+) LKM modunu destekler. GKI 1.0 ve non-GKI çekirdekleri desteklenmez.
- YukiSU yalnızca `arm64-v8a` cihazlarını destekler.
- YukiZygisk, hem `arm64-v8a` hem de `armeabi-v7a` ABI'leri için derleme ve enjeksiyonu destekler.

## Kurulum

Bkz. [`guide/installation.md`](guide/installation.md)

## Entegrasyon

Bkz. [`guide/how-to-integrate.md`](guide/how-to-integrate.md)

## Sorun Giderme

1. Yöneticiyi kaldırdıktan sonra cihaz takılıyor mu?
   _com.sony.playmemories.mobile_ uygulamasını kaldırın.

## Sponsor

- [Anatdx](https://afd.anatdx.moe) (YukiSU geliştiricisi)
- [ShirkNeko](https://afdian.com/a/shirkneko) (SukiSU geliştiricisi)
- [weishu](https://github.com/sponsors/tiann) (KernelSU yazarı)

## Lisans

- “kernel” dizinindeki dosyalar [GPL-2.0-only](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) lisansı altındadır.
- Yukarıda belirtilenler dışındaki tüm diğer kısımlar [GPL-3.0 or later](https://www.gnu.org/licenses/gpl-3.0.html) lisansı altındadır.

## Katkıda Bulunanlar

- [KernelSU](https://github.com/tiann/KernelSU): upstream
- ~~[MKSU](https://github.com/5ec1cff/KernelSU): Magic Mount~~
- ~~[RKSU](https://github.com/rsuntk/KernelsU): non-GKI desteği~~
- ~~[KernelPatch](https://github.com/bmax121/KernelPatch): KernelPatch, APatch çekirdek modülü uygulamasının önemli bir parçasıdır~~

<details>
<summary>KernelSU katkıları</summary>

- [Kernel-Assisted Superuser](https://git.zx2c4.com/kernel-assisted-superuser/about/): KernelSU fikrinin kaynağı
- [Magisk](https://github.com/topjohnwu/Magisk): güçlü bir root aracı
- [genuine](https://github.com/brevent/genuine/): APK v2 imza doğrulaması
- [Diamorphine](https://github.com/m0nad/Diamorphine): bazı rootkit teknikleri

</details>
