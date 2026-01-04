# Projekt_sieci
Kamil Michalak 160 127

Mikołaj Guć 160 242

Kompilacja: ./build.sh

# Wisielec na wyścigi przez sieć 
Gracz łączy się do serwera i podaje swój nick (jeśli jest zajęty, serwer prosi
o inny). 

Po zalogowaniu trafia do lobby, gdzie widzi listę dostępnych pokoi
i liczbę graczy w każdym.

Z lobby gracz może wejść do istniejącego pokoju lub utworzyć nowy.
W każdej chwili może wrócić z pokoju do lobby.

Jeśli w pokoju trwa gra, nowy gracz trafia do stanu oczekiwania i dołączy
do kolejnej rundy po jej zakończeniu. 
Jeśli gra nie trwa, gracz widzi listę
osób w pokoju i czeka na start.

Grę może rozpocząć gracz, który najdłużej czeka w pokoju, o ile w pokoju jest
co najmniej dwóch graczy.

Po rozpoczęciu gry każdy gracz otrzymuje własne, niezależne hasło.
Hasła są takie same między graczami.

Gracze nie widzą wzajemnie swoich liter (są zastępowanie X) ale widzą
postęp wisielca każdego z pozostałych graczy.

Celem gry jest jak najszybsze odgadnięcie własnego hasła.

Gracz zgaduje pojedyncze litery.

Jeśli litera występuje w jego haśle, zostaje wpisana we wszystkich
odpowiednich miejscach.

Jeśli litera nie występuje, postęp jego wisielca przesuwa się o jeden etap.

Gracz odpada, gdy jego wisielec osiągnie ostatni etap, ale nadal może obserwować
postępy innych.

Gra kończy się, gdy:
 * wszyscy gracze odgadną swoje hasła lub
 * wszyscy poza jednym odpadną lub
 * upłynie określony czas rundy.

Po zakończeniu rundy serwer prezentuje ranking czasów wszystkich graczy,
którzy odgadli swoje hasła, a gracze którzy odpadli są oznaczeni jako DNF.

Następnie możliwa jest kolejna runda, o ile w pokoju nadal są gracze.
