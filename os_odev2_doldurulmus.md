---- GRUP ----

>> Grup üyelerinizin isimlerini ve e-posta adreslerini doldurun.

Buse Ozan <2024280046@ogr.deu.edu.tr>
Seyhan Hacer Öztürk <2024280068@ogr.deu.edu.tr>
Yaren Çelebi <2024280062@ogr.deu.edu.tr>

---- AÇIKLAMA ----
>> Pintos dokümantasyonu, ders kitabı, ders notları dışında
>> başvurduğunu ve ders personeliz offline veya online kaynakları belirtin.

Yalnızca Pintos dokümantasyonu ve ders notları kullanılmıştır.

               ARGÜMAN VERME
              ================

---- VERİ YAPILARI ----

>> A1: Her yeni veya değiştirilen `struct' ya da `struct' üyesi, global veya statik değişken,
>> `typedef' ya da enumerasyonun bildirimini buraya yapın. Her birinin amacını 25 kelimeyle açıklayın.

process.c içinde tanımlanan yardımcı yapı:

    struct exec_aux {
      char *fn_copy;             /* process_execute'den start_process'e iletilen komut satırı kopyası. */
      struct child_info *ci;     /* Yeni oluşturulan child için ebeveyn tarafından tahsis edilen bilgi kaydı. */
      struct semaphore load_sema;/* Ebeveyn, child yüklenip sema_up yapılana kadar bu semafor üzerinde bekler. */
      bool load_success;         /* start_process tarafından load() başarısını ebeveyne bildirmek için kullanılır. */
      tid_t parent_tid;          /* Child thread, ebeveynini process_exit sırasında bulmak için saklar. */
    };

thread.h içinde tanımlanan yapı:

    struct child_info {
      tid_t tid;                 /* Bu kayda ait child thread'in kimliği. */
      int exit_status;           /* Child'ın process_exit'te set ettiği çıkış kodu. */
      bool waited;               /* Ebeveyn daha önce bu child için wait çağırdıysa true olur. */
      struct semaphore wait_sema;/* Ebeveyn, child bitene kadar bu semafor üzerinde bekler. */
      struct list_elem elem;     /* Ebeveynin children listesine dahil olmak için liste elemanı. */
    };

thread struct'ına eklenen alanlar:

    struct list children;        /* Bu thread'in tüm child_info kayıtlarının bağlı listesi. */
    tid_t parent_tid;            /* Ebeveyn thread'in kimliği; process_exit'te arama için kullanılır. */
    int exit_status;             /* Thread'in çıkış kodu; SYS_EXIT ve process_exit tarafından set edilir. */
    struct file *fd_table[128];  /* Dosya tanımlayıcı tablosu; indeks FD numarasına karşılık gelir. */
    int next_fd;                 /* Bir sonraki boş FD yuvası; 2'den başlar (0=stdin, 1=stdout). */
    struct file *exec_file;      /* Çalışmakta olan binary'nin file pointer'ı; yazma koruması için tutulur. */

---- ALGORİTMALAR ----

>> A2: Argüman ayrıştırmayı nasıl gerçekleştirdiğinizi kısaca açıklayın.
>> argv[] elemanlarının doğru sırada olmalarını nasıl sağlıyorsunuz?
>> Yığın sayfasının taşmasını nasıl engelliyorsunuz?

process.c'deki push_arguments() fonksiyonu argüman geçişini gerçekleştirir.
İlk adımda, komut satırı strtok_r() ile boşluklara göre ayrıştırılır ve
her token'ın göstericisi argv[] dizisinde saklanır (en fazla 128 argüman).

Doğru sırayı garantilemek için iki aşamalı bir yöntem kullanılır:
  1. Önce argüman stringleri yığına SONDAN BAŞA kopyalanır (argv[argc-1]'den
     argv[0]'a). Her argüman için yazılan yığın adresi arg_ptrs[] dizisine kaydedilir.
  2. Ardından argv pointer dizisi de yığına SONDAN BAŞA yerleştirilir (argv[argc-1]
     göstericisinden argv[0] göstericisine). Böylece main()'in beklediği
     argv[0..argc-1] düzeni doğal olarak oluşur.

Son olarak argv göstericisi, argc ve sahte dönüş adresi push edilir.

Yığın taşması koruması için argüman sayısı 128 ile sınırlandırılmıştır.
Ayrıca stringer ve pointer'lar PGSIZE (4096 bayt) büyüklüğündeki tek bir
palloc sayfasına kopyalanarak komut satırının bir sayfayı aşması engellenir.
Böylece kullanıcı yığınına yerleştirilen veri miktarı en fazla bir sayfa
kadardır; ilk yığın sayfası (4 KB) genellikle bu sınır içinde kalır.

---- GEREKÇE ----

>> A3: Pintos neden strtok_r() fonksiyonunu implement ederken strtok() fonksiyonunu implement etmemiştir?

strtok(), ilerleme durumunu (bir sonraki token'ın nerede başlayacağını)
global/statik bir değişkende saklar. Pintos'ta aynı anda birden fazla
thread çalışabilir; iki thread aynı anda strtok() çağırırsa bu paylaşılan
durum bozulur ve yanlış ya da çöken sonuçlar ortaya çıkar.
strtok_r() ise ilerleme göstericisini çağıranın sağladığı save_ptr
değişkeninde tutar. Her thread kendi save_ptr'ını yönettiğinden fonksiyon
yeniden girişlidir (reentrant) ve iş parçacığı güvenlidir.

>> A4: Pintos'ta, çekirdek komutları çalıştırılabilir bir ad ve argümanlar olarak ayırır.
>> Unix benzeri sistemlerde, kabuk bu ayrımı yapar.
>> Unix yaklaşımının en az iki avantajını belirtin.

1. Esneklik ve soyutlama: Kabuk bu ayrımı yapınca glob genişletme (*.c),
   ortam değişkeni ikamesi ($HOME), yönlendirme (> dosya) ve pipe (|) gibi
   gelişmiş özellikler çekirdek değiştirilmeden uygulanabilir. Çekirdek
   sadece ham argüman listesini alır; karmaşık parsing mantığı kullanıcı
   uzayında kalır.

2. Güvenlik ve küçük güvenilir çekirdek: Parsing kodu çekirdekte çalışırsa
   oradaki bir hata tüm sistemi çökertebilir. Kabuğun bu işi yapması,
   çekirdekteki kod yüzeyini azaltır; parsing hatası en kötü ihtimalle
   yalnızca o kabuğu etkiler, sistemi değil.

                SİSTEM ÇAĞRILARI
                ============

---- VERİ YAPILARI ----

>> B1: Her yeni veya değiştirilen `struct' ya da `struct' üyesi, global veya statik değişken,
>> `typedef' ya da enumerasyonun bildirimini buraya yapın. Her birinin amacını 25 kelimeyle açıklayın.

syscall.c içinde tanımlanan global değişken:

    struct lock filesys_lock;
    /* Tüm dosya sistemi işlemlerini serileştiren global kilit; Pintos dosya sistemi
       thread-safe olmadığından eş zamanlı erişimleri engeller. */

thread.h ve thread struct'ındaki ek alanlar A1'de açıklanmıştır.

>> B2: Dosya tanımlayıcılarının açık dosyalarla nasıl ilişkilendirildiğini açıklayın.
>> Dosya tanımlayıcıları tüm işletim sistemi genelinde mi yoksa yalnızca tek bir işlemde mi benzersizdir?

Her thread (süreç) kendi fd_table[128] dizisini taşır. FD numarası bu
dizinin indeksidir; o indeksteki değer ilgili açık struct file* göstericisidir.
FD 0 stdin, FD 1 stdout için ayrılmıştır; open() sistemi FD 2'den başlayarak
next_fd değişkeni aracılığıyla bir sonraki boş yuvayı atar.

FD'ler yalnızca tek bir işlem içinde benzersizdir. Farklı süreçler aynı
sayısal FD'ye sahip olabilir; bu FD'ler birbirinden bağımsız ve farklı
dosyalara (ya da aynı dosyanın farklı açık örneklerine) işaret edebilir.

---- ALGORİTMALAR ----

>> B3: Çekirdekten kullanıcı verilerini okuma ve yazma kodunuzu açıklayın.

Kullanıcı işaretçileri syscall_handler'a gelmeden önce check_valid_ptr()
ile doğrulanır. Bu fonksiyon şu üç koşulu sırayla kontrol eder:
  1. İşaretçi NULL değil,
  2. is_user_vaddr() ile kullanıcı alanında (PHYS_BASE altında),
  3. pagedir_get_page() ile mevcut sürecin sayfa tablosunda fiziksel
     karşılığı var.
Dört baytlık değerler için adresin her baytı ayrı ayrı kontrol edilir.
Çok baytlı tamponlar (read/write) için check_valid_range() ile başlangıç
ve bitiş adresleri doğrulanır. Herhangi bir doğrulama başarısız olursa
süreç exit_status=-1 ile sonlandırılır.

>> B4: Bir sistem çağrısı, kullanıcı alanından çekirdeğe 4,096 baytlık bir veriyi kopyalıyorsa,
>> bu sayfa tablosunun (örneğin pagedir_get_page() çağrıları) en az ve en fazla kaç kere
>> denetlenmesi gerektiğini açıklayın.
>> Peki, yalnızca 2 baytlık veriyi kopyalayan bir sistem çağrısı için nasıl bir durum olur?
>> Bu sayılar üzerinde iyileştirme yapılabilir mi?

4.096 bayt için:
  - En az 1 kez: Verinin tamamı tek bir sayfa içindeyse bir kontrol yeterlidir.
  - En fazla 4.096 kez: Mevcut implementasyonumuz check_valid_range() ile
    her baytı check_valid_ptr() üzerinden tek tek doğrular; her çağrı 4 kez
    pagedir_get_page() yapar. Bu en kötü durumda 4.096 × 4 = 16.384 çağrıya
    yol açar.

2 bayt için:
  - En az 1, en fazla 2 × 4 = 8 pagedir_get_page() çağrısı yapılır (2 baytın
    iki farklı sayfaya yayılma durumu).

İyileştirme: Her bayt yerine yalnızca tampon başlangıcı ve bitiş sayfası
kontrol edilebilir (en fazla 2 pagedir_get_page() çağrısı, bağımsız sayfa
sayısından). Bu, 4.096 baytlık bir tampon için kontrolü en fazla 2 çağrıya
indirir. Sayfa hataları (page fault) ile erişim doğrulaması yapan alternatif
yaklaşım ise kontrol başına sıfır ek çağrı gerektirir; ancak daha karmaşık
bir hata işleyici gerektirir.

>> B5: "wait" sistem çağrısının implementasyonunu kısaca açıklayın ve işlem
>> sonlandırma ile nasıl etkileşime girdiğini belirtin.

SYS_WAIT, process_wait(pid)'yi çağırır. Bu fonksiyon ebeveynin children
listesini tarar; child_tid'i eşleşen child_info kaydını arar. Kayıt
bulunamazsa veya daha önce wait çağrılmışsa (waited==true) -1 döner.
Aksi hâlde waited=true işaretlenir ve sema_down(&child->wait_sema)
ile child bitene kadar bloke olunur.

Child tarafındaki process_exit() şu adımları izler:
  1. Çıkış mesajını (name: exit(status)) yazdırır.
  2. Açık tüm dosyaları kapatır, exec_file'a yazma iznini geri verir.
  3. Ebeveyn thread'i thread_foreach ile bulur.
  4. Ebeveynin children listesindeki ilgili child_info kaydına exit_status'u
     yazar, ardından sema_up(&ci->wait_sema) ile bekleyen ebeveyne sinyal
     gönderir.
  5. Sayfa dizinini yok eder.
Böylece process_wait, sema_down'dan döner ve child_info'daki exit_status'u
çağırana iletir.

>> B6: Kullanıcı tarafından belirtilen bir adreste kullanıcı programı belleğine yapılacak
>> her erişim, kötü bir işaretçi değeri nedeniyle başarısız olabilir. ...
>> Bu sorunları yönetmek için benimsediğiniz stratejiyi açıklayın. Bir örnek verin.

Stratejimiz "erişimden önce doğrula" yaklaşımıdır. Tüm doğrulama mantığı
check_valid_ptr() ve check_valid_range() adlı iki yardımcı fonksiyonda
merkezîleştirilmiştir. Sistem çağrısı işleyicisi argümanları okumadan
önce bu fonksiyonları çağırır; böylece asıl işlevsel kod hata kontrolü ile
karışmaz ve okunabilirliği korur.

Örnek — write sistem çağrısı:
  1. check_valid_ptr(f->esp + 4)  → fd argümanının adresi doğrulanır.
  2. check_valid_ptr(f->esp + 8)  → buffer pointer'ının adresi doğrulanır.
  3. check_valid_ptr(f->esp + 12) → size argümanının adresi doğrulanır.
  4. check_valid_ptr(buffer)       → tampon başlangıcı doğrulanır.
  5. Asıl write mantığı (putbuf veya file_write) çalıştırılır.

Herhangi bir adımda doğrulama başarısız olursa check_valid_ptr() doğrudan
exit_status=-1 set edip thread_exit() çağırır; çağrı zinciri geri sarılarak
thread temizleme fonksiyonları (process_exit) devreye girer. process_exit,
fd_table'daki tüm açık dosyaları kapatır ve exec_file'ı serbest bırakır.
Eğer sistem çağrısı filesys_lock'u çoktan edinmişse — örneğin buffer
doğrulaması lock alındıktan sonra yapılsaydı — kilit asla bırakılmazdı.
Bu sorunu önlemek için kilidi yalnızca tüm doğrulamalar geçtikten sonra
ediniyoruz; böylece bir doğrulama başarısız olduğunda kilit tutulmamış
olur ve kaynak sızıntısı gerçekleşmez.

---- SENKRONİZASYON ----

>> B7: "exec" sistem çağrısı, yeni çalıştırılabilir dosya yüklenmeden önce dönemez.
>> Kodunuz bunun nasıl garanti eder? Yükleme başarı/durum bilgisini "exec" çağrısını
>> yapan işleme nasıl iletirsiniz?

process_execute(), yığında bir struct exec_aux nesnesi oluşturur; bu yapı
bir semaphore (load_sema, 0 ile başlatılır) ve bir bool (load_success) içerir.
thread_create() yeni thread'i başlatır; hemen ardından ebeveyn
sema_down(&aux.load_sema) ile bloke olur.

Yeni thread start_process()'te çalışır: load() tamamlandıktan sonra
aux->load_success = success atar ve sema_up(&aux->load_sema) ile ebeveyni
uyandırır. Ebeveyn, sema_down'dan döndüğünde load_success'i okur; false
ise child_info kaydını temizleyip TID_ERROR döner, true ise child'ın tid'ini
döndürür.

>> B8: P ana işlemi ile C çocuk işlemi düşünün. P, C çıkmadan önce wait(C) çağırırken
>> doğru senkronizasyonu ve yarış durumlarını nasıl engellersiniz?
>> C çıktıktan sonra nasıl? ...

C çıkmadan önce P wait(C) çağırırsa: P, child_info kaydında wait_sema üzerinde
sema_down() ile bloke olur. C sonunda process_exit'e girdiğinde exit_status'u
child_info'ya yazar ve sema_up() ile P'yi uyandırır. P ardından exit_status'u
okur. Semaphore, "C bitti" sinyalini en fazla bir kez verir ve "P okumadan
kaybolur" yarış durumunu engeller.

C çıktıktan sonra P wait(C) çağırırsa: C daha önce sema_up() çağırmıştır;
semaforun sayısı 1'dir. P'nin sema_down() çağrısı hemen geçer ve exit_status
okunur. child_info kaydı P'nin children listesinde kaldığından C tamamen
yok olduktan sonra bile bilgiye erişilebilir.

Tüm kaynaklar: P, sema_down'dan döndükten sonra child_info'yu listeden
çıkarıp free() ile serbest bırakabilir (mevcut implementasyonda bu adım
eksiktir; iyileştirme olarak eklenebilir). C'nin kendi sayfa dizini
process_exit'te pagedir_destroy ile yok edilir. Açık dosyalar da aynı
fonksiyonda kapatılır.

P, C çıkmadan önce sonlanırsa: P'nin process_exit'i çalışır; ancak
children listesindeki child_info kayıtları serbest bırakılmaz (mevcut
implementasyonda). C daha sonra ebeveyn thread'ini thread_foreach ile
arar; ebeveyn artık mevcut değilse aux.found==NULL olur ve sema_up
çağrılmaz — bu durumda child_info kaydı sızdırılır. Tam bir implementasyonda
P'nin çıkışı, çocukların wait_sema'larını uyandırmalı ya da child_info
kayıtlarını "orphan" olarak işaretlemelidir.

---- GEREKÇE ----

>> B9: Kullanıcı belleğine çekirdekten erişimi, seçtiğiniz şekilde implement etmenizin
>> nedeni nedir?

"Erişimden önce doğrula" yaklaşımını seçtik çünkü uygulaması daha basittir
ve hatalar açık bir şekilde işlenir. Sayfa hatası tabanlı yaklaşım daha
yüksek performans sunabilir; ancak çekirdek sayfa hata işleyicisine ek
mantık eklenmesini gerektirir. Doğrulama mantığını iki küçük fonksiyonda
merkezîleştirmek, her sistem çağrısında tekrar eden guard kodu yazmayı
önler ve bakımı kolaylaştırır.

>> B10: Dosya tanımlayıcıları tasarımınızın avantajlarını veya dezavantajlarını
>> nasıl görüyorsunuz?

Avantajlar:
- Sabit boyutlu dizi (128 girdi) sayesinde O(1) FD araması yapılır.
- Uygulama sadeliği: dinamik bellek yönetimi gerekmez.
- FD alanı her süreç için izole olduğundan güvenlik sorunları sınırlıdır.

Dezavantajlar:
- Süreç başına açık dosya sayısı 128 ile sabit sınırlıdır; bazı uygulamalar
  için yetersiz kalabilir.
- Kapalı FD'ler geri kazanılmaz (next_fd yalnızca ilerler); bu, uzun
  ömürlü süreçlerde tablo dolmasına neden olabilir.
- Her thread struct'ı her zaman 128 × 4 = 512 bayt FD alanı taşır,
  açık dosya olmasa bile.

>> B11: Varsayılan tid_t'den pid_t'ye yapılan eşleme kimlik eşlemesidir.
>> Eğer bunu değiştirdiyseniz, yaklaşımınızın avantajları nelerdir?

Bu eşlemeyi değiştirmedik; tid_t doğrudan pid_t olarak kullanılmaktadır.
Pintos'ta her süreç tek bir thread içerdiğinden bu bire-bir eşleme
yeterlidir ve gereksiz karmaşıklıktan kaçınılır.
