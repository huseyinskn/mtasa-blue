# Inception Games Client

Bu dal, Inception Games Limited için MTA:SA istemcisi ve isteğe bağlı sunucu koruması içerir.

## Windows x86 Release

1. Visual Studio 2026 Build Tools, C++ masaüstü bileşenleri ve Windows SDK kurulu olmalı. Bu dalın kaynak dosyaları Windows SDK başlıklarını kullanır. Upstream derleme akışı DirectX SDK dosyalarını da kullanır. [MTA Windows derleme kılavuzu](https://github.com/multitheftauto/mtasa-blue#build-instructions).
2. Gerekirse DirectX SDK yolunu PowerShell oturumunda `$env:DXSDK_DIR` ile belirt.
3. Depo kökünde aşağıdaki komutları çalıştır. Derlemeden sonra `Bin` altındaki MTA başlatıcısını test için kullandığın GTA: San Andreas kurulumuyla aç.

```powershell
.\win-create-projects.bat
msbuild .\Build\MTASA.sln /m /p:Configuration=Release /p:Platform=Win32
.\win-install-data.bat
```

## Sunucu anti-cheat

Değiştirilen sunucu derlemesinin etkin `mtaserver.conf` dosyasında `<inception_enforce_client_event_limit>1</inception_enforce_client_event_limit>` ayarla ve sunucuyu yeniden başlat. Kota, mevcut `player_triggered_event_interval` ve `max_player_triggered_events_per_interval` ayarlarını kullanır. Varsayılan değerler oyuncu başına 1.000 ms ve 100 olaydır. Pencere, oyuncunun ilk olayıyla veya önceki pencere dolduktan sonra gelen ilk olayla açılır. Kota dolduktan sonra gelen çağrılar Lua işleyicilerine iletilmez. Sunucu otomatik ban uygulamaz.

Korumalı element verileri için MTA’nın `elementdata_whitelisted` ayarı ve `setElementData` erişim ilkeleri kullanılabilir. Para, envanter ve yönetim işlemlerinde oyuncu kimliğini `client` üzerinden al ve işlem yetkisini sunucuda doğrula. [MTA betik güvenliği](https://wiki.multitheftauto.com/wiki/Script_security) ve [element verisi ilkeleri](https://wiki.multitheftauto.com/wiki/SetElementData).

## FPS karşılaştırması

`inception/resources/inception_framebench` klasörünü test sunucusunun `mods/deathmatch/resources` dizinine kopyala. Sunucu konsolunda `refresh` ve `start inception_framebench` çalıştır. Oyuncu konsolunda `inceptionbench 60` komutu ortalama FPS, p95 ve p99 kare süresi ile 33,3 ms üzerindeki kare sayısını verir.

Upstream ve değiştirilmiş istemciyi aynı sunucu, konum, rota, çözünürlük, FPS sınırı ve VSync ayarıyla karşılaştır. Her derleme için birkaç tekrar al. Metin ağırlıklı bir HUD ve yoğun nesne akışı olan bir bölge, bu dalın değiştirdiği yolları çalıştırır.

Değişiklikleri commit ederken amacını, uygulama gerekçesini ve yaptığın testleri mesajda belirt. Depo lisansı ve upstream atıfları [LICENSE](LICENSE) dosyasındadır.
