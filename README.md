# AI Level Studio  (Geode • Geometry Dash 2.2081)

Bir şarkı seç, zorluğu seç → mod şarkıyı dinler, siyah ekranda gerçekten hesaplar/dener, **dekorlu bir level** üretir.

## Kullanım

1. **Create** menüsündeki **AI Level Studio** (logo) butonuna bas.
2. Şarkı listesinden bir şarkı seç (oyunun kendi şarkıları + daha önce indirdiklerin; üstteki kutudan isim veya şarkı ID'si ile ara). Şarkının **▶** butonuna bas.
3. Açılan menüde:
   - **Start at**: müziğin neresinden başlayacağı; **Level length**: oradan itibaren ne kadarının level olacağı
   - **Zorluk**: Easy → Extreme Demon (10 ikon)
   - **Extreme Demon** seçince: **Demon List Top** kutusu (örn. `35` veya `top 35`), hızlı seçenekler (1000/500/250/100/50/10/1) ve **UP** = *Upcoming* (Top 1'den zor, bol frame perfect)
   - **Frame perfect** aç/kapa (**Top 250 ve daha zorunda** açılır; Upcoming'de de var)
   - **Decoration**: Hell / Green / Yellow / Music (müziğe göre) / None (layout)
4. **Generate level** → ekran kararır, yapay zeka şarkıyı analiz eder, parçaları besteler, fizik simülasyonuyla dener, dekor ekler → level açılır.

Mod ayarları (Geode → mod → ayarlar): **Thinking depth** (1–5, varsayılan 4), **When the level is ready** (level sayfası / editör), **Sync offset (ms)**, **Lower the level** (levelı kaç blok aşağı indireceği, varsayılan 2 — hâlâ havadaysa artır), **Use slopes** ve **Mirror slopes** (slope'lar ters/yanlış duruyorsa aç).

## Derleme (GitHub Actions ile, bilgisayara bir şey kurmadan)

1. GitHub'da yeni bir repo aç ve bu klasörün **içindekileri** yükle (`.github` klasörü de dahil).
2. `mod.json` içinde `id` (`kullaniciadin.ai-level-studio`), `developer` ve `repository` alanlarını kendine göre değiştir.
3. **Actions** sekmesinde *Build Binaries* kendiliğinden çalışır. Bitince **Build Output** dosyasını indir, içindeki `.geode` dosyasını Geode'un `mods` klasörüne at.
4. Derleme hata verirse Actions logundaki **ilk kırmızı hata satırını** kopyalayıp bana at, düzeltirim.
   - Log "geode version" uyuşmazlığından şikayet ederse `mod.json` içindeki `"geode"` değerini logda yazan SDK sürümüne eşitle.
   - Bu proje **Geode v5.x** (GD 2.2081) için yazıldı. Yakında `latest` SDK v6 olursa workflow'daki `build-geode-mod` adımına `sdk: v5.11.2` ekle.

## Dürüst notlar (bilmen gerekenler)

- **"Yapay zeka" bir sohbet botu değil.** Müzik analizi (tempo, vuruşlar, bölümler, ses şiddeti) + her parça için yüzlerce varyant besteleyen, simülasyonla test eden bir level üreticisi. Siyah ekrandaki yazılar gerçekten yapılan işi gösterir; bekleme süresi sahte değil (derinlik ayarı arttıkça daha çok varyant dener).
- **"Top 35'e bakıyor"**: mod gerçek Top 35 levelini indirip incelemiyor. Top sırasını (Top 1500 → Top 1, log ölçeğinde) bir zorluk profiline çeviriyor: engel yoğunluğu, hız, koridor darlığı, tıklama penceresi, frame perfect olasılığı. Profil tabloları `src/core/Profile.hpp` içinde, elle ayarlı tahmin değerler; oyundaki zorluğu beğenmezsen oradan değiştir.
- **Frame perfect**: artık tüm level değil; Drop/Build bölümlerinde birkaç sıkı an (simülasyonda en dar 3 karelik pencere), her birinden sonra kolay kalıplar gelir. Simülasyon gerçek oyunun yaklaşığı olduğu için 1–2 karelik pencere hiç kullanılmaz.
- **Doğrulama**: Küp bölümleri 60 fps'lik küçük bir fizik modeliyle "herhangi bir tıklama dizisi bunu geçebilir mi" diye denenir, geçilemeyen yer otomatik onarılır; gemi/UFO/dalga koridorları da uçulabilirlik için kontrol edilir. Bu model gerçek motorun yaklaşığıdır; kesin hakem oyundur.
- **Test durumu**: `src/core` (analiz, üretici, simülasyon, dekor, paketleme) bu ortamda derlenip test edildi (`tests/run_all.sh`; gzip çıktısı Python zlib ile doğrulandı). `src/gd` (Geode arayüzü, FMOD, level kaydı) Geode SDK'sı olmayan ortamda yalnızca bir taklit (mock) başlıkla sözdizimi/tip kontrolünden geçti; **gerçek oyunda ilk derleme/çalıştırma bende test edilemedi.**
- **Modlar**: Cube, Ship, UFO, Wave + hız portalları. Ball/Robot/Spider/Swing yok. Gemi/UFO/dalga koridorlarında artık slope, spike, diş sıraları, havada duran engeller, zigzag, merdiven ve melodiyi izleyen yollar var.
- **Slope'lar**: slope'un oyundaki varsayılan yönünü (ID 289) test edemedim; ters duruyorsa ayarlardan **Mirror slopes**'u aç, hiç olmuyorsa **Use slopes**'u kapat.
- **Hız**: Extreme Demon da dahil her zorlukta 1x/2x/3x/4x karışır; sakin bölümler yavaş, drop'lar hızlı.
- **Dekor**: bölüm başına palet, her ölçüde renk kayması, vuruşa göre nabız, drop'larda flaş + ekran sarsıntısı, zemin üstünde asma çubuklar/sarmaşıklar/ışıklar/testereler (Hell'de sarkıtlar), drop'larda zikzak tavan, koridor duvarlarında parlak kenar. Layout'ta dekor yok. (Testere ID'leri 88/89/98, sarsıntı tetikleyici ID'si 1520: farklı görünürse `src/core/Sim.hpp` içinden değiştirilir.)
- **Şarkılar**: oyunun kendi şarkıları + indirilmiş özel şarkılar. İndirilmemiş ID için önce editörün şarkı tarayıcısından indirmen gerekir. Android'de oyunun kendi şarkıları APK içinde olabilir; okunamazsa mod genel bir ritim ızgarasına düşer (siyah ekranda yazar). Şarkı **önizleme** (dinletme) yok.
- Dekor/koridor/nesne ID'leri GD 2.2 level formatına göre yazıldı; oyunda bir şey yanlış görünürse (renk, portal yüksekliği vb.) tek yerden düzeltilebilir: `src/core/Sim.hpp` (ID'ler), `src/core/Decor.cpp` (renkler).

## Proje yapısı

```
mod.json, CMakeLists.txt, logo.png, about.md, changelog.md
resources/ai-logo.png            mod içi logo
src/main.cpp                     Create menüsüne buton
src/gd/                          Geode/GD kodu: şarkı listesi, FMOD decode, popup'lar, siyah ekran, level kaydı
src/core/                        taşınabilir çekirdek (oyun başlığı yok, PC'de test edilir)
  Audio.*      FFT, spectral flux, tempo/beat, bölüm tespiti
  Sim.*        fizik modeli: küp botu, tıklama penceresi, koridor kontrolü
  Patterns.*   engel kütüphanesi (pencere genişliği simülasyonla ölçülür)
  Profile.hpp  zorluk / Top sırası → sayılar
  Gen.*        planlama, stiller, varyant üretimi, seçim, onarım, koridor süslemeleri
  Decor.*      paletler, tetikleyiciler, nabızlar
  LevelString.*  GD level metni + gzip/base64 (kendi yazdığımız, zlib ile doğrulanmış)
  Studio.*     hepsini sırayla çalıştıran tek fonksiyon
tests/                           çekirdek testleri (tests/run_all.sh), tests/mock = sadece sözdizimi kontrolü için taklit başlıklar
```
