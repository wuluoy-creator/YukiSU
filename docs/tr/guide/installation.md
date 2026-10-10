# Kurulum, güncelleme ve kurtarma

[Türkçe belgeler](../README.md) · [Ayrıntılı İngilizce kılavuz](../../guide/installation.md)

Manager APK'sini yüklemek tek başına root sağlamaz. Uygun LKM'yi içeren yamalı açılış imajı da cihaza yazılmalıdır.

## Gereksinimler

- ARM64, Android 12 / API 31 veya üstü ve Linux 6.1 veya üstü.
- Hazır LKM hedefleri: `android14-6.1`, `android15-6.6`, `android16-6.12`, `android17-6.18`. KMI'yi yalnızca Android sürümüne göre seçmeyin.
- Açılış bölümlerini yazma ve kurtarma yöntemi; genellikle kilidi açılmış bir bootloader gerekir.
- Kurulu yazılımla eşleşen orijinal imajlar ve doğru bölüm / A/B yuvası bilgisi.

Aşağıdaki komutlar yalnızca bilgi okur:

```sh
adb shell uname -r
adb shell getprop ro.product.cpu.abi
adb shell getprop ro.build.version.sdk
adb shell getprop ro.boot.slot_suffix
```

KMI listesi derleme hedeflerini gösterir; her cihazın doğrulandığı anlamına gelmez.

## Standart LKM kurulumu

1. Manager APK'sini [yayımlanan sürümlerden](https://github.com/wuluoy-creator/ZySU/releases) indirin. Actions kullanıyorsanız son çıktı `Manager-arm64-v8a` olmalıdır.
2. Ana ekrandaki durum alanından kurulumu açın. Cihazın ramdisk düzenine göre orijinal `boot.img` veya `init_boot.img` seçin.
3. Gömülü LKM'yi ya da hedef çekirdekle eşleşen özel `.ko` dosyasını kullanın. Manager hedef KMI'yi belirleyemezse elle seçim ister. `init_boot` çekirdek içermediği için tek başına KMI bilgisi vermeyebilir.
4. Gerekirse SuperKey ayarlayın ve yamayı çalıştırın. Dosya yöntemi çıktıyı cihazın `Download` dizinine kaydeder; seçilen dosyayı otomatik olarak bölüme yazmaz.
5. Günlüğü inceleyin, çıktıyı cihazınıza uygun araçla doğru bölüm ve yuvaya yazın, ardından yeniden başlatın.

Standart `boot-patch`, ksuinit ve LKM'yi ramdisk içine yerleştirir; modül açılışın erken aşamasında yüklenir. HTTPS yazılım arşivlerinden açılış imajı çıkarma seçeneği de vardır. Arayüzün gerçekten tespit ettiği biçim ve bölümleri kontrol edin.

Mevcut root erişimiyle doğrudan kurulum kullanılabilir. A/B cihazlarda etkin olmayan yuvaya kurulum da görünür. Bu seçenek, OTA'nın yazıldığı hedef yuvanın imajı ve KMI'siyle kullanılmalıdır. Doğrudan kurulum bölümlere yazar.

## LKM'yi doğrudan boot çekirdeğine gömme

Yerel dosya ve doğrudan kurulum yöntemlerinde `boot-patch-v2` seçilebilir. Yalnızca çekirdek içeren `boot.img` / `boot` hedeflenir; `init_boot` ve `vendor_boot` bu yöntemin hedefi değildir.

Gömülü LKM seçiminde hedef çekirdekten KMI okunur; standart KMI seçim ekranı kullanılmaz. Çekirdek biçimi, semboller ve ABI denetlendiğinden her boot imajı kabul edilmez. Bu yöntem `CONFIG_KSU=y` desteği sağlamaz. Yöntem değiştirirken aynı yuvadaki eski ramdisk yamaları da temizlenebilir; ilgili orijinal imajları ve günlükleri saklayın.

CLI'de `boot-patch --out` bir dizin, `boot-patch-v2 --output` ise dosya alır. Ayrıntılar [CLI belgesindedir](../../ksud-cli.md).

## Kimlik doğrulama ve ilk açılış

SuperKey ayarlanmadıysa Manager imza yapılandırmasına göre tanınır. Kendi imzanızla ürettiğiniz APK için uygun LKM güven yapılandırması gerekir; [entegrasyon kılavuzuna](how-to-integrate.md) bakın.

SuperKey ayarlandıysa kurulumda belirlenen anahtarı kullanın. İmza atlama seçeneği, Manager için yalnızca SuperKey doğrulamasını seçer; Linux modül imza denetimini kapatmaz. “Shell'e izin ver”, Android shell UID'sinin root istemesine izin verir; adbd seçeneğinden ayrıdır.

Yeniden başlattıktan sonra çekirdeğin tanındığını, kimlik doğrulamayı, UAPI uyumunu ve ksud sürümünü kontrol edin. Gerekirse ksud kartından paketlenen programı eşitleyin. SUMHP yerleşiktir: `auto` uygun OverlayFS'yi, ardından Magic Mount'u seçer; SUMH açıkça seçilir. OverlayFS / Magic Mount değişiklikleri genellikle yeniden başlatma gerektirir.

## Güncelleme ve kurtarma

Manager resmi sürümleri kontrol eder; CI güncelleme kanalı yoktur. APK, ksud ve çekirdek modülü ayrı bileşenlerdir. APK güncellemesinden sonra gerektiğinde ksud'yi eşitleyin, açılış imajını yeniden yamalayın ve cihazı yeniden başlatın. OTA sonrası hedef KMI'yi tekrar kontrol edin.

Kaynak kodu ve sonraki sürümler, herkese açık `wuluoy-creator/ZySU` deposunda birleştirilmiştir. Eski APK hâlâ `ZySU-Releases` deposunu kontrol ediyorsa yeni güncelleme adresiyle derlenmiş bir APK'yi [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases) üzerinden bir kez elle yükleyin. Mevcut kurulumun üzerine güncelleme için aynı imza ve daha yüksek sürüm kodu gerekir. Eski APK'yi yeni depoya kopyalamak içindeki adresi değiştirmez. Eski depo, geçmiş kayıtları ve geçiş bilgileri için korunur.

İmajı geri yükleme ve kalıcı kaldırma farklı işlemlerdir. Geri yükleme, eşleşen yedekleri veya ilgili yamayı kaldırma yolunu kullanır. Kalıcı kaldırma userspace ve modülleri siler, açılış imajını geri yüklemeyi dener, Manager'ı kaldırır ve yaklaşık beş saniye sonra yeniden başlatma ister. Mevcut uygulama geri yükleme başarısız olsa da devam eder; başlamadan önce orijinal imajları ve harici kurtarma yöntemini hazırlayın. APK'yi silmek açılış bölümlerini geri yüklemez.

Cihaz açılmıyorsa değiştirilen bölüm ve yuvaya yazılımla eşleşen orijinal imajları geri yükleyin. Modül kaynaklı sorunlarda güvenli mod yardımcı olabilir: erken açılış hook'u en az üç ses kısma basışı algıladığında userspace modülleri devre dışı bırakır. Bu algılama zamanlamaya bağlıdır ve imaj kurtarma yönteminin yerini tutmaz.

Hata bildirirken cihazı, yazılımı, KMI'yi, bileşen sürümlerini, yama yöntemini, yuva bilgisini ve günlükleri ekleyin. `ksud install` yalnızca kullanıcı alanı bileşenlerini yükler; açılış imajı kurulumunun yerine geçmez.
