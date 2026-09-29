![GitHub Logo](http://www.heise.de/make/icons/make_logo.png)

Maker Media GmbH

***

# The Blue Mermaid – Tragbares Flipperspiel

### Ein selbstgebauter Flipper mit ESP32, WS2812B-LEDs, Laser-Lichtschranke und Sound – ein Projekt für Groß und Klein.

Ein tragbares Flipperspiel für ein Festival zu bauen – das allein ist schon eine Herausforderung. Noch schwieriger wird es, wenn die eigenen Kinder mitbauen sollen. Wie das gelingt, wird im Artikel in der Make 5/26 beschrieben.

Im gedruckten Schaltplan (Bild 9, S. 24) sind die Lautsprecher falsch angeschlossen. Sie dürfen nicht mit VCC oder GND verbunden werden. Der DFPlayer Mini hat einen eingebauten Verstärker — beide Lautsprecher werden ausschließlich zwischen SPK_1 und SPK_2 angeschlossen (parallel, ohne externe Stromversorgung). Der korrigierte Schaltplan liegt hier im Repository.

![Picture](https://github.com/MakeMagazinDE/Flipper/blob/master/BM_GitHub.png)

Der vollständige Artikel zum Projekt steht in der [Make-Ausgabe 5/26](https://www.heise.de/select/make/2026/5). Hier finden Sie den Arduino-Sketch, die Lasercutter-Vorlagen und die 3D-Druckdateien.
