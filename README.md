lcron
=====

Praca na zaliczenie

Program czyta plik konfiguracyjny podany w parametrze `-c` (domyślnie jest to
/etc/lcron.conf). Format jest w większości taki jak pierwotnym cronie.
Przetworzenie pliku skutkuje dodaniem poprawnych wpisów do listy zadań.
Demon wyszukuje zadań, które powinny zostać uruchomione w najbliższym czasie
i zasypia. Wybudza się gdy by uruchomić te zadania, wyszukuje kolejnych zadań
do uruchomienia i zasypia. Proces jest powtarzany dopóki demon działa.

Wszystkie informacje zapisywane są do logu (/tmp/lcrond.log).
By dostać bardziej szczegółowe informacje na temat pracy demona w argumentach
należy dopisać `-v`, by dostać jeszcze bardziej szczegółowe `-vv`.

Program można również uruchomić bez przejścia w tryb demona: `-f`.

Uruchomienie, zakończenie i status zakończenia zadania odnotowywany jest w logu.
Strumienie stdout i stderr przekierowywane są do plików o nazwach
job[nr_zadania_w_pliku_konfiguracyjnym].stdout (... .stderr) w katalogu /tmp.

W programie użyłem biblioteki anacron'a matchrx.h na licencji GNU.


Format pliku konfiguracyjnego
-----------------------------

    M H d m w
    * * * * * Polecenie
    | | | | Dzień tygodnia (1 - 7) (Niedziela=7)
    | | | Miesiąc (1 - 12)
    | | Dzień w miesiącu (1 - 31)
    | Godziny (0 - 23)
    Minuty (0 - 59)

Dostępne konfiguracje pól:
- liczba - praca zostanie wykonana gdy wartość będzie taka jak w polu
- gwiazdka - dane pole pasuje do każdej wartości
- zakres - uruchomienie nastąpi gdy wartość mieści się w zakresie
np. `5-10` (5, 6, 7, 8, 9)
- podział np. `*/20` (w polu minut: 0, 20, 40)
- zakres z podziałem - pasuje gdy np. `10-25/5` (10, 15, 20)

Istnieje również możliwość łączenia tych konfiguracje oddzielając je przecinkami.


    32 18 * * * ls /home

wyświetli katalogi użytkowników codziennie o 18:32.

    0 0,10-20/4 * * * cp -ur /etc /tmp/etc

uaktualni wszystkie pliki w katalogu /tmp/etc z tymi w /etc co 4 godziny
od 10 do 20 lub gdy godziną będzie północ.


Instalacja
----------

`$ make && sudo make install`

Program zostanie zainstalowany w /usr/bin.


Dezinstalacja
-------------

`$ sudo make remove`
