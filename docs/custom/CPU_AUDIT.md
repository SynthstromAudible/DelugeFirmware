# Deluge MDD + Plaits: hata ve CPU taraması

İncelenen temel: Deluge-Plaits-Optimized-20261011.zip içindeki a33f3b0c642a85825c13de4c25ef604df2d06ad2 kaynak görüntüsü. Bu paket kaynak düzeltmesi ve host test sonuçlarıdır; yeni BIN veya ELF içermez. Bu turda ARM derlemesi ve fiziksel cihaz testi yapılmadı. Tüm firmware için hatasızlık sertifikası değildir.

## Kaynakta düzeltilenler

1. **Aşırı yükte geçersiz iterator erişimi.** `AudioEngine::killOneVoice`, synth voice listesi boşken audio clip fallback'ini çağırdıktan sonra devam edip end iterator'ünü dereference ediyordu. Fallback'ten sonra return eklendi. Song bulunmayan durum da güvenle dönüyor. Öncelik sırası ve mevcut voice azaltma eşikleri değişmedi.
2. **MDD WRITE sırasında ses işleme açlığı ve sınırsıza yakın iş.** Offline bake boyunca ses rutini çağrılmıyordu. Artık 32 aday adımda bir `routineWithClusterLoading(false)` çalışıyor. Kullanıcı olayları bu ara çağrıdan işlenmiyor; kısmen hazırlanmış sequence'e UI yeniden girmiyor. Notasız/mute sequence'lerde nota sayısı sınırı işe yaramadığından bake'e 1.048.576 aday adım sınırı eklendi. 65.536 nota sınırı da korunuyor. İş başarısızsa geçici notalar atılıyor, eski grid ve MDD korunuyor. Mevcut hata dönüşü INSUFFICIENT_RAM; işlem bütçesinin aşıldığı durum için ayrı OLED mesajı henüz yok. Bu hizmet aralığı matematiksel gerçek-zaman garantisi değildir; SD ve gerçek cihaz testine ihtiyaç var.
3. **Plaits sonlu sayı koruması.** Global `-ffast-math` altında `std::isfinite` güvenilir koruma değildir. Float IEEE-754 exponent bitleri kontrol edilerek NaN/sonsuz değerler sıfırlanıyor, normal örnekler aynı -1..1 aralığında tutuluyor. Bu bir dayanıklılık düzeltmesi; motorların normal kullanımda NaN ürettiği iddiası değildir. Sonraki bloklarda motor iç durumunu sıfırlamaz.

## CPU incelemesinin sonucu

- Plaits motoru her etkin voice, unison parçası ve Plaits osilatörü için ayrı render ediliyor. 4 nota × 4 unison × 2 Plaits OSC, yaklaşık 32 motor örneği demektir; filtre ve efekt maliyetleri buna eklenir. Bu sayı CPU yüzdesi değildir.
- Plaits nota başlangıcında `allocMaxSpeed`, voice bırakılırken dealloc kullanıyor. Bu, özellikle yoğun akorlarda anlık yük ve bellek parçalanması adayıdır. Başarısız allocation kontrol ediliyor. Henüz havuz uygulanmadı.
- Sabit perdede log2 dönüşümü zaten cache'leniyor. Kullanılmayan AUX render yolları önceki optimizasyonda kapatılmış; bu turda yeni hız kazancı olarak sayılmadı.
- FM oversampling korunuyor. Otomatik kalite düşürme, gizli unison/polyphony sınırı veya oversampling azaltma eklenmedi.
- Model değişimi heap allocation yapmıyor, ayrılmış engine alanını yeniden kuruyor. Çok sık model modülasyonu yine reset/init maliyeti taşır. Değişmeyen modelde bu işlem atlanıyor.
- MDD core sabit boyutlu 7 lane üzerinde transport tick'inde çalışıyor; audio sample başına heap allocation yapmıyor. Custom kaynaklar yalnızca değer değiştiğinde patch değişim bitini işaretliyor. MDD'nin bütün projelerde ihmal edilebilir CPU maliyetinde olduğu ölçülmedi; çok sayıda clip ve yüksek tick çözünürlüğü ayrıca ölçülmeli.
- Native aşırı yük koruması var: scheduler süresinden direness hesaplıyor, düşük öncelikli sesleri hızlandırılmış release veya kesme ile azaltıyor. Bu geç aşama kurtarma mekanizması; sınırsız polyphony garantisi değil.
- Mevcut High CPU Usage Indicator, culling olduğunda PLAY LED'ini uyarı için kullanıyor. Sürekli CPU yüzdesi göstergesi veya bütün underrun'ların sayacı değildir.
- OLED ve gridin cihaz üzerindeki zaman payı bu turda ölçülmedi. Çizim, SD erişimi ve sample streaming de gerçek yük bütçesinde hesaba katılmalı.

## Darboğazı önlemek için sıradaki geliştirme önceliği

1. **Ölçüm:** gerçek Deluge üzerinde audio render tepe süresi, deadline kaçırma ve voice culling sayacı. Sadece ortalama yeterli değil. Ses döngüsünde log/ekran çizimi yapmadan sayaç topla; UI daha seyrek göstersin.
2. **Voice bütçesi:** bu ölçümlerle motor maliyetini, unison'u ve iki osilatörü dikkate alan Plaits bütçesi belirle. Önce bırakılmış ve düşük öncelikli voice'ları fade ederek azalt; kullanıcı polyphony sınırını görsün. Bu turda uygulanmadı.
3. **Önceden ayrılmış bellek havuzu:** note-on/off heap işini sabit süreli slot alma/bırakmaya taşı. Havuz sınırı, RAM tüketimi ve voice stealing davranışı önce tasarlanmalı.
4. **Arka plan işi:** uzun WRITE için eklenen cooperative servicing'i donanımda doğrula; gerekirse küçük parçalara bölünmüş job'a taşı. UI çizimlerini değişim bazlı ve sınırlı sıklıkta çalıştır.
5. **Kalite seçenekleri:** ancak ölçümden sonra, açıkça seçilen Economy/High Quality modu değerlendir. FM oversampling azaltmak tını ve aliasing'i değiştirir; otomatik uygulanmamalı.

Pratik kullanımda ilk deneme: Plaits unison 1, gerekli değilse tek Plaits OSC ve daha kısa release. Ağır katmanları audio'ya render etmek sürekli sentez yükünü azaltabilir fakat SD streaming yükü oluşturur. Güvenli maksimum ses sayısı cihaz ölçümü olmadan verilmez.

## Doğrulama

- Gerçek killOneVoice fonksiyon gövdesi, kontrollü Sound/Song modelleriyle C++ debug iterator ve UBSan altında: boş liste, Song yok, audio fallback, voice önceliği ve release sonuçları.
- Gerçek WRITE/Undo gövdeleri: 34 allocation hata noktasında rollback, eski notalar/ifade/direction geri alma; ses servicing çağrısında UI reentrancy kapalı.
- Bake servicing açık/kapalı aynı nota olayları; tamamen mute dev clip için iş sınırı.
- NaN, ±sonsuz ve normal clipping: `-O3 -ffast-math` testinde.
- MDD regresyonları: menü, sync, grid, scale, Custom sinyal/depth, codec, bake ve başlangıç.
- Plaits ASan/UBSan: 3 motor, macro köşeleri, değişken buffer boyları, 1000 canlı model değişimi, guard örnekleri, AUX primary eşitliği, A4 yaklaşık 440 Hz. Menü testi ayrıca çalıştırıldı.

Host testleri embedded donanımın zamanlamasını, allocator'ünü ve bütün scheduler etkileşimlerini taklit etmez. Bu paketten doğrudan cihaza yükleme yapılmaz; kaynak ARM ile derlenip donanımda doğrulanmalıdır.
