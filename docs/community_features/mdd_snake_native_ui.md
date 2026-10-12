# MDD — arpeggiator tarzı yatay OLED pencereleri

Bu sürüm arpeggiatorun kullandığı gerçek `HorizontalMenu` sınıfını kullanır. MDD içinde yatay görünüm OLED'de zorunludur; global Horizontal Menus seçeneğinin açık olmasını gerektirmez.

SYNTH/KIT/MIDI/CV ekrandaki dört sütunun parametresini seçer; SELECT'i çevirmek seçilen parametreyi değiştirir. SCALE/CROSS SCREEN çok sayfalı pencerelerde sayfayı değiştirir. BACK pencereyi kapatır/üst seviyeye döner. Bu dört sütunlu davranış yalnızca açık MDD menülerinde geçerlidir. Ana clip ekranında stock preset/sound edit kontrolleri kullanılır.

Grid altında tanımlanan kontrol padlerine basmak pencereyi hemen açar; SELECT onayı gerekmez. Dizilim boşluksuz, sola yaslı 10 paddir: Notes, Gates, Velocity, Custom, Random, Reset, Root/Scale, Map, MDD Snake, Write. Random All/Reset All aynı padlerin OLED penceresindedir. Root/Scale ortak penceredir. MDD Snake WRITE’ın hemen solundadır ve Direction seçili ilk sayfayı açar. Range Settings üçüncü sayfasındadır.

| Pencere | SYNTH | KIT | MIDI | CV |
| --- | --- | --- | --- | --- |
| Notes/Gates/Velocity | Step | Step Value | Steps | Shift |
| Custom | Custom 1–4 | Step | Step Value | Steps |
| Settings sayfa 1 | Direction | Speed | Steps | Shift |
| Settings sayfa 2 | Snake Shape | Sequencer | Lane | Custom |
| Map | Slot | Target | Depth | Custom |
| Range | Minimum | Maximum | Steps | Shift |
| Root/Scale | Root | Scale | — | — |
| Random sayfa 1 | Random (iki sütun) | Random | Random All (iki sütun) | Random All |
| Random sayfa 2 | Minimum | Maximum | — | — |
| Reset sayfa 1 | Reset (iki sütun) | Reset | Reset All (iki sütun) | Reset All |
| Reset sayfa 2 | Steps | Shift | — | — |

Random/Reset padleri yalnızca pencere açar; ilgili action seçiliyken SELECT veya aynı sütun tuşuna tekrar basmak stock horizontal menu action davranışıyla uygular.

4×4 hücreyi basılı tutup SELECT çevirmek, 350ms beklemeden o hücrenin değerini değiştirir ve `Notes Step 3: 69` benzeri popup gösterir. Popup seçili diziyi, step numarasını, değeri ve varsa yüzde birimini içerir. Çevirme yapıldıysa bırakmak mute tetiklemez. Kısa bas/çek mute; en az 350ms tutmak değer penceresini açar. Step1+N uzunluk chord'u korunur ve bu chord sırasında değer düzenleme yapılmaz.

Normal moda dönerken MDD'nin grid görüntüsü ve occupancy verileri temizlenir, tick overlay kapatılır ve stock grid tam yeniden çizilir. Önceki ortak native tick/sync düzeltmeleri, sönük mute kareler, yedi bağımsız dizi, Custom mapping ve Plaits korunur. Custom 1–4 varsayılan olarak None'dır; prewired hedef yoktur.

## Doğrulama sınırı

Host testleri production MDD menü/edit/dispatch kodu ile native HorizontalMenu encoder yöntemini çalıştırır; LCD/physical button seçiminin bazı çevre hizmetleri doubles kullanır. Native grid koduyla Normal dönüşünde eski MDD renklerinin temizliği kontrol edilir. ARM Release derlemesi uygulanır. Fiziksel Deluge üzerinde burada denenmedi.


## WRITE ve synth erişimi (son güncelleme)

MDD ana ekranında SELECT basma / çevirme ve SYNTH/MIDI/CV kontrolleri stock preset ve sound edit davranışını kullanır. BACK menüyü kapatır veya ana ekrandan sound editorü açar; sequencer modu değişmez. MDD menüsüne Notes/Gates/Velocity/Custom veya diğer kontrol padlerinden girilir. MDD'yi kapatmak için Sequencer → Normal seçilir (WRITE onayı da yazdıktan sonra Normal'e geçer).

4×4 altındaki son, 10. pad WRITE'tır. Range hâlâ MDD Settings üçüncü sayfasından erişilebilir. WRITE → “Replace stock notes?” → SELECT ile No/Yes → SELECT basarak onay. No/BACK veri değiştirmez. Yes mevcut clip uzunluğunu koruyarak tick 0'dan bir loop üretir; Notes, Gates ve Velocity lane'lerinin bağımsız speed/direction/shape/shift/mute ayarlarını uygular. Song root/scale aktifse aynı nota quantizasyonu kullanılır. Gate normal nota uzunluğudur; sonraki trigger veya loop sonu gate'i keser. Random yön o loop için sabitlenir.

Yazım önce geçici nota satırlarında hazırlanır. Bellek yetmezse eski normal dizi ve MDD modu korunur. Başarılı yazım mevcut normal nota satırlarını (satıra bağlı MPE/otomasyon dahil) değiştirir, önceki satırları native Undo/Redo snapshot’ında saklar ve mevcut undo geçmişini korur ve Normal'e döner. Synth preset'i ve clip genel parametreleri korunur. MDD lane değerleri saklanır. Custom 1–4 modülasyon kaynakları ve matrix eşlemeleri normal nota gridi verisi değildir; bu sürüm bunları normal parametre otomasyonuna bake etmez. WRITE Notes/Gate/Velocity aktarımıdır.

WRITE yürüyüş yönlerini nota sırasına bake ettiği için normal clip yönünü Forward yapar; aksi halde Reverse/Pingpong ikinci kez uygulanırdı. Çalma fazı MDD native tick sayacından clip uzunluğuna göre alınır; song clock geri sarılmaz.


## İlk açılış düzeltmesi

Yeni clip'in yedi lane'i müzikal 1/16 hızla başlar. Song verilmeden oluşturulan clip'in 24-tick varsayımı, MDD girişinde ve playback öncesinde gerçek Song quarter-note çözünürlüğüne bağlanır. UI yalnızca sayıyı değiştirmez; lane tick süreleri de güncellenir. İlk aktivasyon bu sürümde kaydedilmiş hızları 1/16 ile başlatır; aynı oturumdaki sonraki mod geçişleri düzenlediğin hızları korur. Codec'in ayrılmış byte'ı clock çözünürlüğünü saklar; eski MDD verilerinin native timing alanları okunmaya devam eder.

Root değeri Song root'unun 12'ye göre nota sınıfıdır; örneğin 60=C, 62=D. Root ilk çizimde başlıkta görünür, Root menüsünde de 0–11 aralığında okunur. Root'u değiştirmek gerekmez.

Random/Random All ve Reset/Reset All action'ları ikişer sütun kaplar ve tam adlarını üst satırda gösterir. SYNTH veya KIT ilk action'ı; MIDI veya CV All action'ını seçer. Minimum/Maximum ve Steps/Shift ikinci sayfadadır; SCALE/CROSS SCREEN sayfa değiştirir.


## Kompakt kontrol sırası

| Pad (soldan) | İşlev |
|---|---|
| 1 | Notes — MIDI sütununda Steps, SELECT ile 1–16 |
| 2 | Gates — MIDI sütununda Steps, SELECT ile 1–16 |
| 3 | Velocity — MIDI sütununda Steps, SELECT ile 1–16 |
| 4 | Custom — CV sütununda Steps; seçili Custom 1–4 uzunluğu |
| 5 | Random — OLED'de Random / Random All |
| 6 | Reset — OLED'de Reset / Reset All |
| 7 | Root / Scale ortak pencere |
| 8 | Map — seçili Custom için synth mod matrix eşlemesi |
| 9 | MDD Snake — üç sayfalı Settings, ilk sayfa Direction/Speed/Steps/Shift |
| 10 | WRITE — No/Yes |

Sonraki padler söndürülür ve kontrol olarak tepki vermez. İlk dört lane padinin Steps alanı ilgili lane'in 1–16 uzunluğunu düzenler; Custom'da önce Custom 1–4 seçilir. 4×4 step1+N uzunluk gesture'ı da korunur.

Map: Custom padinden Custom 1–4 seç; Map aç. SYNTH = Slot (1–4), KIT = Target, MIDI = Depth (-100…+100%), CV = Custom. Her Custom için dört hedef slotu vardır. None eşlemeyi devre dışı bırakır. Hedef listesi stock patch matrix'in bu kaynağa izin verdiği synth parametrelerini içerir; Notes/Gates/Velocity burada eşlenmez. Custom step değerleri unipolar 0–100% kaynak seviyesidir; Depth yön ve miktardır. Hedefin temel değeri korunur, kaynak ona modülasyon ekler. Envelope zamanları/filtre/OSC gibi izinli yerel hedefler seçilebilir. Global send reverb tipi/decay veya patch edilemeyen parametreler listede yoktur. Plaits Engine seçilirse gösterim 1–3 olur; gerçek engine sonucu temel engine değeri ve Depth'e de bağlıdır. Aynı Custom içinde aynı hedef ikinci slotta tekrar edilemez. Önceden eklenen aynı kaynak/hedef matrix kablosu varsa Map aynı kabloyu düzenler; ayrı ikinci bir kablo yaratmaz. WRITE Custom kaynaklarını normal otomasyona bake etmez.


Root/Scale ve Custom düzeltmesi (2026-10-09): Root her zaman Song.key.rootNote verisini okur. Root düzenleme, Song.transposeAllScaleModeClips ile ortak tonaliteyi taşır; seçili scale korunur, scale-modundaki clip notaları da transpoze olur. Scale Song.setScale üzerinden proje genelinde değişir. MDD aktifken saklı Normal satırlar scale uyumluluk taramasına dahil edilmez; MDD zaten Song scale'ine quantize edilir. Diğer aktif Normal scale clip'lerindeki nota sayısı yeni scale'e sığmıyorsa seçim geri alınır ve “Scale cannot fit notes” gösterilir. Normal moda dönünce saklı satırlar tekrar taramaya katılır.

Custom 1–4 unipolar kaynaklardır. Yeni kablolar global polarity tercihinden bağımsız Unipolar kurulur; 0–100% tamamı kullanılır. Eski kayıtlardaki Bipolar tercihi de Custom için 0–100% anlamını değiştirmez; Custom sabit unipolar kaynaktır. Negatif yön Depth ile seçilir. Stock kaynakların polarity davranışı değişmez. Map'te seçilebilir ama mevcut synth yapılandırmasında çalışmayan hedefler “Target inactive” mesajı verir (ör. kapalı oscillator). Map mutlak parametre ataması değildir: sonuç temel parametre + Depth ve kaynak etkisidir; hedef sınırına dayanmışsa duyulur değişim olmayabilir. Depth=0 ve tüm step değerleri=0, Unipolar bağlantıda etki üretmez.


## Activation, rhythm, SCALE and WRITE undo — 2026-10-10

At the first MDD activation of each fresh or loaded clip all seven lanes are bound to the actual Song clock at 1/16. Loading a clip saved with MDD already enabled applies this immediately. Subsequent mode switches in the same session retain edited lane rates. Saved cell values, mappings, lengths and directions are unchanged. Clones retain the activation state.

Speed includes plain 1/4, 1/8, 1/16, 1/32, 1/64; triplet 1/4T, 1/8T, 1/16T, 1/32T, 1/64T; dotted 1/4D, 1/8D, 1/16D, 1/32D, 1/64D. T is 2/3 the straight duration, D is 3/2. Fractional tick timing and codec denominators support these rates without tempo-dependent rounding.

On the MDD main grid, SCALE toggles quantization using the existing Song key without inferring a new scale from dormant Normal notes; SHIFT+SCALE enables quantization and cycles the Song scales. In open paged MDD menus SCALE remains a page button.

Custom Depth is linear for pitch and delay-rate targets as well: 50% contributes half the full-depth Custom source; stock sources retain their strength curves. Final parameter response still uses Deluge's parameter conversion, base value and bounds.

WRITE creates a native MDD_WRITE undo action owning the previous complete NoteRowVector, including row MPE/automation and sequence direction. Undo restores the old Normal grid, redo restores the baked grid. MDD remains disabled after undo/redo; its lane data remains available. Existing undo history is retained. Snapshot/action allocation failure aborts the write. Custom modulation is still not baked.

The long WRITE confirmation title and selected Map/Scale values use native scrolling, including during playback. Other UI scrolling keeps the stock playback behavior.

MDD uses native portamento. Normal mono/poly portamento can glide from the remembered prior pitch; strict LEGATO presets require overlapping held notes. MDD sends note-off before the next note and does not implement a tie/overlap lane.
