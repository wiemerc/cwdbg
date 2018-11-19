## Allgemeines

### Wie komme ich an die Debug-Informationen? 
* Erster Ansatz: DWARF - geht nicht, es gibt zwar eine gute Bibliothek f. DWARF (https://github.com/aclements/libelfin), aber in C++
* Zweiter Ansatz: Format von SAS/C (nur Tabelle mit Zeilen / Adressen), das auch VBCC / VLINK erzeugen kann - aber wie komme ich an die Variablen?
* Dritter Ansatz: STABS - erscheint erst kompliziert, ist aber nicht so wild
Das Einlesen ist tatsa"chlich nicht so schwierig (mit ein bisschen Spicken im Quellcode der Binutils), aber das Erzeugen eines AST aus den STABS war schon tricky


### Disassembler
Capstone (zum Disassemblieren) lässt sich weder mit dem GCC noch mit dem VBCC für den Amiga kompiliere(Clang fu"r macOS funktioniert), Capstone nutzt aber fu"r M68k auch nur den Code von Musashi, vielleicht kann ich den direkt verwenden


### Wie starte ich das Programm?
* Erster Ansatz: mit CreateNewProc() - funktioniert nicht (keine Ausgabe, CLI wird nach Beenden des Programms geschlossen, man nicht auf das Programm warten), ich brauche aber auch keinen separaten Prozess
* Zweiter Ansatz: Aufrufen des Einstiegspunktes des Targets (als Funktion) - funktioniert


### Wie stoppe ich das Programm?
Entweder mit dem Trace Mode (geht pro Prozess weil bei einem Kontextwechsel das Statusregister gesichert wird) oder mit Breakpoints => Ersetzen der Instruktion an der gewu"nschten Adresse mit TRAP
Programm erzeugt Prozessor-Exception => Exception Handler wird aufgerufen => schickt Signal an Prozess => erzeugt Task Exception => Exception Handler wird aufgerufen => schickt Nachricht an Debugger und wartet auf Antwort
Vereinfachung fu"r CLI-Version: Handler fu"r Task Exception ist zentrale Funktion des Debuggers, die auf Eingaben wartet, Befehle ausfu"hrt usw. (z. B. run())
Handler fu"r Prozessor-Exception ko"nnen kein Wait() aufrufen (laufen im Supervisor Mode) und anscheinend kann man einen Task im AmigaOS nicht anhalten
In der ixemul.library ist die Kommunikation zwischen Debugger und Target anscheinend u"ber Signale gelo"st (bin mir aber nicht sicher).


## Meilensteine
* Projektstart: 28.10.2018
* Einlesen und Auswerten der STABS in Python: 10.11.2018
* Laden eines Programms mit LoadSeg(), Starten als Task / Prozess, schrittweises Ausfu"hren und Breakpoints
* Laden mit eigenem Loader in C einschliesslich der STABS
* Ausgeben von Variablen
* ARexx Port und Integration mit Editor (DME? XDME? GoldED? CygnusEd)


## Links
* <https://eli.thegreenplace.net/2011/01/23/how-debuggers-work-part-1/>
* <https://eli.thegreenplace.net/2011/01/27/how-debuggers-work-part-2-breakpoints/>
* <https://eli.thegreenplace.net/2011/02/07/how-debuggers-work-part-3-debugging-information/>
* <https://opensource.apple.com/source/cctools/cctools-836/as/m68k-opcode.h.auto.html>
* <https://www.atarimagazines.com/v5n3/ExceptionsInterrupts.html>
