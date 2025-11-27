# **Projektplan: Aufenthaltsraum-Tracker mit Live-Visualisierung**

## **Projekttitel**

Aufenthaltsraum-Tracker mit Live-Visualisierung

## **Projektbeschreibung**

Das Projekt zielt darauf ab, die Auslastung des Aufenthaltsraums in Echtzeit zu erfassen und zu visualisieren, um Studierenden die Entscheidung zu erleichtern, wo sie ihre Pause verbringen möchten. Anstatt eine exakte Personenanzahl zu ermitteln, konzentrieren wir uns darauf, das allgemeine Aufkommen und die Aktivität im Raum abzubilden. Dies hilft den Studierenden, besser zu planen und unnötige Wege zu vermeiden.

## **Zielgruppe**

Studierende und Mitarbeitende, die den Aufenthaltsraum nutzen möchten und vorab Informationen über die aktuelle Auslastung erhalten wollen.

## **MVP-Definition**

Unser Minimum Viable Product fokussiert sich auf die Erfassung und Visualisierung des allgemeinen Aufkommens im Aufenthaltsraum, ohne eine präzise Personenzählung anzustreben. Das System soll ausreichend Informationen liefern, damit Nutzende eine fundierte Entscheidung über ihren Aufenthaltsort treffen können.

## **Technische Umsetzung**

### **Sensorik und Datenerfassung**

- **Mikrocontroller-Boards: 2x (ESP32-C6-N8) für Lichtschranke und Mikrofon, 1x separates Board mit e-Ink Display für Visualisierung.**

- **Lichtschranken zur Personenfluss-Messung (3 Sensoren: 2x VL53L0X + 1x VL6180X):** Wir haben drei Distanzsensoren am Treppenaufgang installiert – zwei VL53L0X für die Erkennung von Ein- und Ausgängen sowie einen VL6180X in der Mitte für die Validierung der Bewegung. Dies ermöglicht eine präzise Richtungserkennung (IN/OUT) durch eine 3-Sensor-Sequenzlogik, die Fehlzählungen verhindert und eine zuverlässige Schätzung des Personenflusses liefert. Die Sensoren messen im Bereich von 50-1200 mm und verwenden direkte Blockier-Erkennung statt Bewegungserkennung für höhere Zuverlässigkeit.

- **Mikrofon um die Lautstärke im Raum zu messen (INMP441 I2S):**  
  Wir setzen ein digitales INMP441 I2S Mikrofon ein, das die Lautstärke im Raum misst.

### **Visualisierung**

- **Inkplate 2 Display im Foyer:** Die gesammelten Daten werden in Echtzeit auf einem Inkplate 2 Display im Foyer visualisiert. Das Display zeigt auf einen Blick, ob im Aufenthaltsraum viel los ist oder nicht. Studierende können so sofort entscheiden, ob sie im Aufenthaltsraum essen möchten oder lieber im Foyer oder draussen bleiben. Die Visualisierung soll intuitiv und schnell erfassbar sein, um eine niedrigschwellige Entscheidungshilfe zu bieten.

- **Web-Dashboard:** Parallel zum Inkplate entwickeln wir ein Web-Dashboard, das tiefergehende Einblicke in die Auslastungsmuster bietet. Dieses Dashboard zeigt beispielsweise Stosszeiten, durchschnittliche Auslastung oder Trends über verschiedene Tageszeiten und Wochentage. Es dient als Ergänzung zur Inkplate-Visualisierung und richtet sich an Nutzende, die detailliertere Informationen wünschen.

## **Projektphasen (nur über unsere Übersicht)**

### **Phase 1: Konzeption und Planung**

* Detaillierte Anforderungsanalyse und Definition der Sensoren  
* Auswahl und Beschaffung der Hardware (Lichtschranken, PIR-Sensor, Inkplate 2, Mikrocontroller)  
* Erstellung eines Datenmodells und Festlegung der Kommunikationsprotokolle

### **Phase 2: Prototyping**

* Aufbau und Test der Lichtschranken-Konfiguration (zwei Sensoren zur Richtungserkennung)  
* Integration des PIR-Sensors und Test der Bewegungserkennung  
* Entwicklung der Firmware für Datensammlung und \-übertragung  
* Erste Tests zur Datenqualität und Zuverlässigkeit

### **Phase 3: Datenverarbeitung und Backend**

* Implementierung eines Backends zur Datensammlung und \-speicherung  
* Entwicklung von Algorithmen zur Interpretation der Sensordaten (Auslastungsschätzung)  
* Aufbau einer Datenpipeline zur Echtzeitverarbeitung

### **Phase 4: Visualisierung**

* Entwicklung der Inkplate 2 Anzeige mit klarer, intuitiver Darstellung der Auslastung  
* Gestaltung und Programmierung des Web-Dashboards mit erweiterten Analysefunktionen  
* User Testing und iterative Verbesserung der Visualisierungen

### **Phase 5: Integration und Testing**

* Vollständige Integration aller Komponenten (Sensoren, Backend, Displays)  
* Durchführung von Funktionstests im realen Umfeld  
* Optimierung basierend auf Testerkenntnissen

### **Phase 6: Deployment und Evaluation**

* Installation im Aufenthaltsraum und Foyer  
* Monitoring der Systemstabilität  
* Sammlung von Feedback der Nutzenden  
* Dokumentation des Projekts und Lessons Learned

## **Erwartete Ergebnisse**

* Funktionsfähiger Prototyp zur Erfassung des Aufkommens im Aufenthaltsraum mittels Lichtschranken und Mikrofon.  
* Inkplate 2 oder gleichwertiges Display im Foyer, das in Echtzeit die aktuelle Raumsituation anzeigt  
* Web-Dashboard mit detaillierten Auslastungsanalysen und Trends  
* Verbesserte Nutzererfahrung durch transparente Information über Raumverfügbarkeit

## **Erfolgskriterien**

* Das System liefert konsistent verwertbare Daten über das Aufkommen im Aufenthaltsraum  
* Die Visualisierung im Foyer ist für Nutzende intuitiv verständlich und hilfreich bei der Entscheidungsfindung, ob sich der Gang in den Aufenthaltsraum lohnt.  
* Das Web-Dashboard bietet interessante Einblicke in Nutzungsmuster  
* Positive Resonanz der Studierenden auf das System  
* Technische Stabilität und Zuverlässigkeit über längere Zeiträume

## **Risiken und Herausforderungen**

* Unzuverlässigkeit der Lichtschranken bei starkem Personenverkehr oder Lichtbedingungen  
* Datenschutzrechtliche Hürden bei der Kameranutzung  
* Technische Komplexität bei der Integration mehrerer Sensorsysteme  
* Begrenzte Genauigkeit bei der Auslastungsschätzung ohne exakte Personenzählung

### **Geplante Technologien:**

- Wir arbeiten mit html, css und java script. Wir verwenden keine aufwändigen Frameworks.  
- Unser Frontend wird sehr schlicht. Wir visualisieren die Auslastung auf einem Ink-Display und erstellen ein einfaches Dashboard mit weiterführenden Informationen zur Auslastung des Aufenthaltsraums. Diese sind aber nicht im Foyer sichtbar sondern das muss bei Bedarf oder Interesse selber im web aufgerufen werden. Das Ink-Display stellen wir im Foyer auf, damit es gut sichtbar ist.  
- 

## **Unsere Elemente für das Projekt**

Datenbank. So stellen wir uns unser Backend vor (ERM). Bitte die API-Endpoints erstellen\!

- **Mikrocontroller 1**  
  Mikrocontroller mit zwei Distanzsensoren zur Überwachung des Personenflusses (Bewegen sich Personen in den Raum hinein oder gehen sie raus.

- **Mikrocontroller 2**  
  Hier bringen wir ein Mikrofon an, das die Lautstärke im Raum misst, damit wir anhand der Dezibel-Werte definieren können, ob viele Leute im Raum sind oder nicht.

- **Ink-Display (controller board und separates Display)**  
- Board: Waveshare ESP32 Universal Raw e-Paper Driver Board  
- Display: Waveshare 296x168 2.36inch 4-Farben E-Ink Display  
  Hier visualisieren wir den Personenfluss (entweder eine Zahl wenn wir die Personen zuverlässig erfassen können und sonst eine Info, ob die Auslastung im Raum hoch ist oder niedrig, wir sind nicht zwingend darauf angewiesen, dass wir eine genaue Anzahl Personen erfassen können (je genauer, desto besser)

- **Web-Dashboard**  
  Hier visualisieren wir alle gemessenen Daten auf einer Plattform. Eine einfache Programmierung mit html, css, js.
