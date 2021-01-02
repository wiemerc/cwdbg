## Allgemeines

### Wie komme ich an die Debug-Informationen? 
* Erster Ansatz: DWARF - geht nicht, es gibt zwar eine gute Bibliothek f. DWARF (https://github.com/aclements/libelfin), aber in C++
* Zweiter Ansatz: Format von SAS/C (nur Tabelle mit Zeilen / Adressen), das auch VBCC / VLINK erzeugen kann - aber wie komme ich an die Variablen?
* Dritter Ansatz: STABS - erscheint erst kompliziert, ist aber nicht so wild
Das Einlesen ist tatsächlich nicht so schwierig (mit ein bisschen Spicken im Quellcode der Binutils), aber das Erzeugen eines AST aus den STABS war schon tricky


### Disassembler
Capstone (zum Disassemblieren) lässt sich weder mit dem GCC noch mit dem VBCC für den Amiga compilieren (Clang für macOS funktioniert), Capstone nutzt aber für M68k auch nur den Code von Musashi, vielleicht kann ich den direkt verwenden

Integration war ganz einfach, man braucht nur zwei Dateien patchen.


### Wie starte ich das Programm?
* Erster Ansatz: mit CreateNewProc() - funktioniert nicht (keine Ausgabe, CLI wird nach Beenden des Programms geschlossen, man nicht auf das Programm warten), ich brauche aber auch keinen separaten Prozess
* Zweiter Ansatz: Aufrufen des Einstiegspunktes des Targets (als Funktion) - funktioniert


### Wie stoppe ich das Programm?
Entweder mit dem Trace Mode (geht pro Prozess weil bei einem Kontextwechsel das Statusregister gesichert wird) oder mit Breakpoints => Ersetzen der Instruktion an der gewünschten Adresse mit TRAP

Programm erzeugt Prozessor-Exception => Exception Handler wird aufgerufen => schickt Signal an Prozess => erzeugt Task Exception => Exception Handler wird aufgerufen => schickt Nachricht an Debugger und wartet auf Antwort
Vereinfachung für CLI-Version: Handler für Task Exception ist zentrale Funktion des Debuggers, die auf Eingaben wartet, Befehle ausführt usw. (z. B. run())
Handler für Prozessor-Exception können kein Wait() aufrufen (laufen im Supervisor Mode) und anscheinend kann man einen Task im AmigaOS nicht anhalten

Lösung mit Signal funktioniert nicht weil der Signal Handler nicht sofort nach Rückkehr des Trap Handlers aufgerufen wird (sondern erst wenn der Task Scheduler das nächste Mal läuft).
Neuer Ansatz: Trap Handler setzt Rücksprungadresse im Exception Stack Frame auf Glue Code, der zentrale Routine des Debuggers aufruft

Statusregister muss im Supervisor Mode wiederhergestellt werden und danach muss *direkt* in das Target zurückgesprungen werden. Jede Anweisung im User Mode könnte das Statusregister schon wieder verändern, auch z. B. ein move.

Auch im Supervisor Mode müssen die Interrupts gesperrt werden, sonst kommt es zu verschiedenen, nicht reproduzierbaren Fehlern.

Sporadisch werden im Einzelschrittmodus (auf Assembler-Ebene) Anweisungen "übersprungen", anscheinend im Zusammenhang mit (immer nach?) Branch-Anweisungen. Nicht reproduzierbar, Ursache unbekannt, vielleicht ein Fehler in Musashi.

Das Überspringen von Anweisungen tritt mit FS-UAE auf dem Mac (Amiga OS 3.1) nicht mehr auf => muss irgendwie mit Amiga Forever / Windows zusammenhängen.


### Remote Debugging
Mit FS-UAE ist serielle Kommunikation zwischen Host und Amiga möglich. Packen / Entpacken der Pakete am besten mit der Methode aus _The Practice of Programming_


## Meilensteine
* 28.10.2018:   Projektstart
* 10.11.2018:   Einlesen und Auswerten der STABS in Python
* 13.01.2019:   Laden eines Programms mit LoadSeg(), Ausführen, Setzen von Breakpoints, schrittweises Ausführen, Disassembler, Anzeigen von Registern und Stack
* 14.12.2020    Rudimentäre Kommunikation über serielle Schnittstelle implementiert
* 02.01.2021    Refactoring zur Vorbereitung der Implementierung des Remote Debuggings abgeschlossen
* ??.??.2021:   Remote Debugging mit Host in Python
* ??.??.2021:   Schrittweises Ausführen und Setzen von Breakpoints auf C-Ebene
* ??.??.2021:   Ausgeben von Variablen


## Links
* <https://eli.thegreenplace.net/2011/01/23/how-debuggers-work-part-1/>
* <https://eli.thegreenplace.net/2011/01/27/how-debuggers-work-part-2-breakpoints/>
* <https://eli.thegreenplace.net/2011/02/07/how-debuggers-work-part-3-debugging-information/>
* <https://opensource.apple.com/source/cctools/cctools-836/as/m68k-opcode.h.auto.html>
* <https://www.atarimagazines.com/v5n3/ExceptionsInterrupts.html>
* <http://www.mrjester.hapisan.com/04_MC68/>


## Sonstiges
Remote Shell mit AUX:
* auf Amiga-Seite: `mount aux:` und `newcli aux:`
* auf Mac-Seite: `socat STDIO,raw,echo=0 TCP:127.0.0.1:1234`
