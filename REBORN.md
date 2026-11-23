# Reborn Roleplay

Başlatıcıdaki **Oyuna Gir** düğmesi paket içindeki MTA:SA 1.6 istemcisini açar ve `141.98.50.226:22003` adresindeki Reborn Roleplay sunucusuna bağlanır. Başlatıcı yalnızca MTA takma adını hatırlar. Hesap girişi ve karakter seçimi oyun içinde yapılır. SQL bağlantısı mevcut VDS sunucu resource’unda kalır.

## Windows x86 Release

1. Visual Studio 2026 Build Tools (v145) veya 2022 Build Tools (v143), C++ masaüstü bileşenleri ve Windows SDK kurulu olmalı. Bu dalın kaynak dosyaları Windows SDK başlıklarını kullanır. Upstream derleme akışı DirectX SDK dosyalarını da kullanır. [MTA Windows derleme kılavuzu](https://github.com/multitheftauto/mtasa-blue#build-instructions).
2. Gerekirse DirectX SDK yolunu PowerShell oturumunda `$env:DXSDK_DIR` ile belirt.
3. Depo kökünde aşağıdaki komutları Visual Studio Developer PowerShell üzerinden çalıştır. x86 istemci ve x86 sunucu `Bin` altında üretilir. ZIP paketinde `Reborn Roleplay.exe` dosyasını aç. Kaynaktan derlediğinde aynı başlatıcı `Bin/Multi Theft Auto.exe` adıyla oluşur.

```powershell
.\win-create-projects.bat
msbuild .\Build\MTASA.sln /m /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v145
.\win-install-data.bat
```

## Sunucu anti-cheat

Oyuncular mevcut MTA 1.6 sunucusuna doğrudan bağlanabilir. Aşağıdaki kota, Reborn sunucu derlemesi VDS’ye alındığında çalışır. Mevcut login, karakter ve SQL resource’ları ayrı olarak VDS’de tutulur.

Windows x64 sunucu gerekiyorsa aynı çözümü aşağıdaki hedefle derle. Linux VDS için deponun `linux-build.sh` akışı kullanılır. Oyuncu başlatıcısı Windows x86 olarak kalır.

```powershell
msbuild .\Build\MTASA.sln /m /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145
```

Değiştirilen sunucu derlemesinin etkin `mtaserver.conf` dosyasında `<reborn_enforce_client_event_limit>1</reborn_enforce_client_event_limit>` ayarla ve sunucuyu yeniden başlat. Kota, mevcut `player_triggered_event_interval` ve `max_player_triggered_events_per_interval` ayarlarını kullanır. Varsayılan değerler oyuncu başına 1.000 ms ve 100 olaydır. Pencere, oyuncunun ilk olayıyla veya önceki pencere dolduktan sonra gelen ilk olayla açılır. Kota dolduktan sonra gelen çağrılar Lua işleyicilerine iletilmez. Sunucu otomatik ban uygulamaz.

Korumalı element verileri için MTA’nın `elementdata_whitelisted` ayarı ve `setElementData` erişim ilkeleri kullanılabilir. Para, envanter ve yönetim işlemlerinde oyuncu kimliğini `client` üzerinden al ve işlem yetkisini sunucuda doğrula. [MTA betik güvenliği](https://wiki.multitheftauto.com/wiki/Script_security) ve [element verisi ilkeleri](https://wiki.multitheftauto.com/wiki/SetElementData).

## FPS karşılaştırması

`reborn/resources/reborn_framebench` klasörünü test sunucusunun `mods/deathmatch/resources` dizinine kopyala. Sunucu konsolunda `refresh` ve `start reborn_framebench` çalıştır. Oyuncu konsolunda `rebornbench 60` komutu ortalama FPS, p95 ve p99 kare süresi ile 33,3 ms üzerindeki kare sayısını verir.

Upstream ve değiştirilmiş istemciyi aynı sunucu, konum, rota, çözünürlük, FPS sınırı ve VSync ayarıyla karşılaştır. Her derleme için birkaç tekrar al. Metin ağırlıklı bir HUD ve yoğun nesne akışı olan bir bölge, bu dalın değiştirdiği yolları çalıştırır.

Değişiklikleri commit ederken amacını, uygulama gerekçesini ve yaptığın testleri mesajda belirt. Depo lisansı ve upstream atıfları [LICENSE](LICENSE) dosyasındadır.
