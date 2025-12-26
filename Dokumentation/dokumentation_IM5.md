# **Dokumentation IM5 | Raum-Tracker**

Das Problem kennen wir an der Fachhochschule alle: “Wir wissen um die Mittagszeit nicht, wie hoch die Auslastung im Aufenthaltsraum ist”. Diesem Problem wollten wir uns annehmen.

Wir haben uns zu Beginn zuerst überlegt, wie wir Personen in diesem Raum am besten tracken können. Schnell sind wir auf einen Bewegungssensor gekommen aber haben schnell gemerkt, dass es schwierig ist eine zuverlässige Auslastung zu tracken und dann haben wir uns primär für eine Lichtschranke entschieden. Damit sollte ein ziemlich genaues und zuverlässiges Tracking möglich sein. Zusätzlich wollen wir noch die Lautstärke im Aufenthaltsraum messsen, um ein runderes Bild abzubilden. Somit haben wir uns für diese zwei Hauptsensoren entschieden.  
Für das visuelle Feedback haben wir nach e-ink oder e-Displays recherchiert und sind fündig geworden. Dieses Display wird im Foyer aufgestellt, um die vorbeigehenden Studenten direkt zu informieren, ob die Auslastung im Aufenthaltsraum Tief, Mittel oder Hoch ist.

Zusätzlich haben wir ein Dashboard aufgebaut, was Live- und historische Daten aggregiert und visuell sinnvoll anzeigt. Das Dashboard soll vor allem funktional sein und hat uns während der Entwicklung sehr geholfen, da wir unsere Mikrocontroller und die Daten direkt beobachten konnten.

## **Learnings:**

* Spannend war es, einen spezialisierten cron job mit einem Raspberry Pi zu integrieren, der genau auf unsere Bedürfnisse abgestimmt ist. Zuerst wollten wir diesen auf “infomaniak” einrichten, aber dieser konnte nur stündlich ausgeführt werden und wir wollten die Daten während einer gewissen Zeitspanne am Tag mit hoher Frequenz im Aufenthaltsraum minütlich tracken.  
* Das Zusammenspiel von mehreren Mikrocontrollern war neu und wir haben viel über arduino gelernt  
* durch das viele hin und her mit den Controllern, haben wir uns einen workflow eingerichtet, der es uns erlaubt, ohne Kabel und nur über WLAN unsere Sketches hochzuladen. Das ist genial und bequem.  
* Mit der Hilfe von Jan Fiess haben wir es hinbekommen, dass wir uns mit unseren Mikrocontrollern ins “eduroam” Netz einwählen können, das hat uns mehr Flexibilität geben, die Sensoren da zu installieren wo wir sie brauchten. Die Informationen zum Netzwerk haben wir nicht direkt in den arduino-code geschrieben, sondern in eine versteckte Datei und dann auf unseren code referenziert.  
* 

## **Herausforderungen:**

* das Zusammenspiel mit den unterschiedlichen Mikrocontrollern und dem Display war schon sehr herausfordernd, bis wir überhaupt genau wussten, wer mit wem kommunizieren muss  
* Die Integration des Displays und die Inbetriebnahme hat sich schwieriger gestaltet als zu Beginn gedacht. Unser favorisiertes Display war in der Schweiz nicht verfügbar, dann mussten wir weiter suchen und dann haben wir unglücklicherweise unser erstes Display noch “geschlissen” und uns dann nochmal ein neues besorgt.  
* Wir haben sehr viel Zeit in die Ausrichtung und Feinjustierung der Lichtschranke investiert. Wir sind einige Male am Punkt gewesen, dass wir die Lichtschranke aufgeben wollten.

### **Erster Aufbau am 12.11.25**

Wir haben heute das erste Mal unsere Sensoren an den vorgesehenen Orten provisorisch installiert.  
Das war sehr wertvoll, da wir gesehen haben wie unsere Installation unter “echten” Bedingungen besteht.

**Während der Tests gab es einige Herausforderungen:**

- **eduroam Netzwerk:** Unsere Lichtschranken haben wir im Treppenhaus zum Aufenthaltsraum installiert. Die Lichtschranken haben den Personenfluss leider noch nicht zuverlässig getrackt, wir sind uns noch nicht ganz sicher, ob es an den Bedingungen im Treppenhaus liegt (viel Beton, vielleicht schwierig für die Sensoren). Wir haben dann noch weitere Tests mit anderen WLAN-Netzwerken gemacht und haben den Verdacht, dass die Kommunikation über eduroam in diesem “Beton-Bunker” sich schwierig gestalten könnte, da die Sensoren unserer Meinung nach in anderen WLAN-Netzwerken, z.B. “Igloo” zuverlässiger gesendet haben. Wir haben auch versucht, den Beton mit Molton abzudecken, damit es vielleicht besser funktioniert, aber das hat leider nicht geholfen.  
- **Das Mikrofon** das jetzt an unserem Mikrocontroller angebracht ist, sendet leider keine zuverlässigen Daten, auch wenn im Raum sehr viele Leute sind und es sehr laut ist. Anscheinend ist es nicht für eine Dezibel-Messung geeignet, denn wir erhalten überhaupt keinen Ausschlag.

**Unsere nächsten Schritte:**

- einen dritten “VL6180X- Sensor” zwischenschalten, damit das Signal und die Zählung der Ein- und Austritte präziser läuft.  
- Das Mikrofon haben wir mal auf Eis gelegt, da die Lichtschranke Priorität hat.  
- Eine Möglichkeit wäre noch, andere Sensoren von Jan zu verwenden (VL53L1X). Diese sollten besser für Personenzählung geeignet sein.

### **Erster Prototyp für unser Displaygehäuse**

Da wir unser Display im Foyer mobil aufstellen wollen, haben wir uns einen ersten Prototypen erstellt.  
Darin enthalten sind folgende Komponenten:

* Display mit Mikrocontroller  
* Optional: Batterie oder mit einer Powerbank

Wir haben uns fürs erste in Blender ein Gehäuse zusammengebaut, das die Komponenten bestmöglich darin einschließt und wir für die Komponenten genug Platz haben. Blender ist eher weniger für genaue Konstruktionen gedacht, aber für unser Gehäuse sollte das ausreichend sein. Wir haben unsere Komponenten eingebaut und es hat alles drin Platz gefunden. Das Gehäuse ist leicht zu wenig hoch, aber für die ersten Versuche passt's.

Wir passen den Deckel noch an, damit dieser einfach abgemacht werden kann und nicht nur auf der Kante des unteren Gehäuses liegt.

### **Zweiter Aufbau am 27.11.25**

Heute wollten wir unsere Installation wieder im Treppenhaus anbringen und uns den Komponenten Display und Mikrofon widmen. Es stellte sich aber wieder heraus, dass unsere Lichtschranke doch noch nicht so funktioniert, wie wir uns das gedacht hatten. Zuerst Internetprobleme, dann unerklärliche Ausfälle bei den Sensoren. Diese Stolperfallen mussten wir erst aus dem Weg räumen und erst nach dem Mittag lief der Ablauf der Sensoren wieder stabil.

Unsere Lichtschranke besteht jetzt aus 3 Sensoren und wir haben ein neues Mikrofon am Mikrocontroller angebracht (INMP441 I2S).

### **Beobachtungen während einer regulären Schulwoche**

Die Lichtschranke haben wir montiert und beobachten nun während einer regulären Schulwoche die Auslastung des Raumes und wie sich die Sensoren schlagen. Wir haben Stichprobenartig die gemessene Auslastung mit der effektiven Auslastung abgeglichen und kommen zum Schluss, dass wir einen ziemlich starten “Drift” haben, da die Sensoren der Lichtschranke so wie erwartet nicht so zuverlässig alle Ein- und Austritte erfassen.

**Unsere nächsten Schritte:**

- wir versuchen, den “Drift” unserer Sensoren durch eine Feinjustierung bei den Ein- und Austritten anzupassen, damit wir auf eine realistische Anzahl Personen für die Ein- und Austritte kommen.

### **Nachbearbeitung am 9.12.25**

**Drift-Korrektur erfolgreich implementiert**  
Heute haben wir die Drift-Korrektur auf dem Controller und unserer Website implementiert. Die Werte zeigen uns während der nächsten Tage, ob sie funktioniert und wie wir sie noch feinjustieren müssen. Die ersten Beobachtungen während des Tages haben aber gezeigt, dass die Chance auf realistischere Werte absolut gegeben sind.

**Display-Anpassungen gemacht und verfeinert**  
Wir haben die Anzeige nochmals vereinfacht, indem wir die Raumauslastung und die ungefähre Personenanzahl nur noch in einem Feld anzeigen lassen. Danach mussten wir alle Werte nochmals finetunen, damit das Display Änderungen in der Raumauslastung und der Lautstärke zuverlässiger anzeigt. Die Farben haben sich die Werte sehr wohl zu gelb (“mittel”) und rot (“hoch”) verändert, nur waren die Werte für diese Veränderungen noch zu hoch.

Konkret bedeutete dies, wir mussten in der generate\_occupancy\_snapshot.php Datei die Schwellenwerte der Raumauslastung und des Mikrofons nochmals neu definieren, damit sie auf dem Display richtig angezeigt werden. Weitere Veränderungen mussten wir am Mikro-Code vornehmen (Siehe nächster Punkt). 

**Mikrofon unter realen Bedingungen getestet**  
Lange war das Problem beim Mikrofon, dass es aus der Nähe getestet zuverlässig ausschlug, sobald im Raum installiert aber zu wenig Schall daran war \- der Dezibel-Wert in unserer Anzeige (online & Display) stieg äußerst selten an. Dieser Dezibel-Wert ist aber eigentlich nur ein künstlich umgewandelter Dezibel aus den RAW-Werten, welche das Mikrofon am Arduino erfasst. Wir haben das System nun ein bisschen ausgetrickst und den Bereich vom minimalen (leise) und maximalen (laut) Zustand näher zueinander genommen, es aber dabei belassen, dass die Anzeige den Wert in dB anzeigt.

Dafür haben wir a) die "noiseLevels-Werte" im generate\_occupancy\_snapshot.php angepasst und b) im Arduino Code des Mikrofons die oben erwähnten Werte näher zueinander genommen und den Gain-Faktor auf sieben erhöht. Das Mikrofon schlägt somit schneller und heftig aus.

Der angezeigte minimale Wert liegt bei 30dB der maximale bei 60dB.

### **Was funktioniert gut und was nicht?**

Das Endergebnis beim **Displays** ist für uns beide zufriedenstellend. Der einzige Wermutstropfen ist die langwierige Refresh Time (ca. 15s), was auf eine Aktualisierungsrate von all 90 Sekunden halt gerade einen zusätzlichen Sechstel der gesamten Anzeigezeit einnimmt. In Online-Foren hiess es, dass die Refresh Time bei Verwendung von schwarz-weiss Treibern kürzer werden könnte, dem war aber nicht so beziehungsweise ich konnte keinen nützlichen Unterschied feststellen.  
Diese technische Einschränkung lässt sich leider nicht überwinden, ausser möglicherweise durch den Kauf eines teureren Produkts.

Das **Mikrofon** wurde durch die neueste Anpassung der Werte vom Sorgen- zum Wunderkind.   
Es schlägt erstaunlich zuverlässig aus, auch bei nur wenigen Leuten im Raum. Dass der Snapshot eine Momentaufnahme macht und keine Langzeitmessung ist, ist ein zu vernachlässigendes Problem. Wenn viele Leute miteinander reden, muss es schon sehr unglücklich verlaufen, dass sie genau im Moment des Snapshots ruhig sind. Insgesamt muss man aber eingestehen, dass diese I2S-Mikrofone nicht zur Dezibelmessung konzipiert wurden und dies auch nicht wirklich können. Deshalb ist die Umwandlung in einen künstlichen Dezibelwert ein Gebastelt \- Mikrofone, die diese Funktion bieten, sind für uns aber deutlich zu teuer (ca. 100 Fr.).  
So wie es jetzt läuft, ist für uns absolut zufriedenstellend.

Die **Lichtschranke** war und ist das Herzstück des Projekts.  
Gleichzeitig hat uns nichts so viel Mühe bereitet wie diese Sensoren.  
Unser Problem bestand darin, dass die Prototypen immer wieder gut funktionierten, aber am eigentlichen Nutzungsort nicht mehr fehlerfrei. Dies kann gemäss Foren und YT-Videos verschiedenste Gründe haben, der am häufigsten genannte (auch von Jan) ist die Reflektion von Oberflächen. Jedoch habe ich die Sensoren im Code auf eine max. Distanz eingestellt (1.2m), welche kürzer ist als der Abstand zur nächsten Wand. Online steht geschrieben, dass auch das nicht 100% gerade Aufsetzen der Linsenkappen zu unzuverlässigen Ergebnissen führen kann. Weiter gäbe es auch noch andere Modi, um diese Sensoren zu betreiben, aber ich verstehe nicht, wie ich auf diese zugreifen kann. Wie gesagt, es gibt viele mögliche Gründe, wieso es nicht reibungslos funktioniert hat.  
Fakt ist \- und das haben wir in den Prototypen, sowie in den Online-Blogs erfahren \- dass es mit unseren Sensoren zwar möglich ist, aber es sicher bessere Methoden und Geräte zur Raummessung gäbe (z.B. ein PIR Sensor)

### **Fazit**

Am Schluss bleibt ein Projekt, welches zwar nicht fehlerfrei läuft, aber während dem wir enorm viel lernen durften. Mit mehr Zeit und Ressourcen liesse sich dieses Projekt auch mit fehlerfreier Funktionalität verwirklichen. Und einfach so mal im Studium eine Raummessung mit Front-, Backend und Komponenten des physical computing komplett selbst erstellt/programmiert zu haben, ist schon very f\*cking nice\!