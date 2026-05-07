# STM32-Bidirectional-Motor-Control-OLED

Control Bidirecțional al unui Motor DC si afisarea starii pe un afisaj OLED (STM32)

### Video Proiect
[![Video Proiect](https://img.youtube.com/vi/iiBoR9vVaX8/hqdefault.jpg)](https://youtube.com/shorts/iiBoR9vVaX8)

Acest proiect demonstrează controlul turației și direcției de rotație pentru un motor de curent continuu (DC). Sistemul folosește un microcontroller STM32 pentru a genera semnale PWM și o punte H (Motor Driver) pentru a inversa polaritatea, oferind feedback vizual în timp real pe un ecran OLED

__Cum funcționează?__
Direcția (Sensul de rotație): Pentru a schimba direcția motorului, microcontroller-ul trimite comenzi logice (1 și 0) către driver-ul de motor. Acesta inversează electronic polaritatea tensiunii aplicate pe motor (Punte H), făcându-l să se rotească fie în sens orar, fie antiorar 

Viteza (Turația): În loc să dăm motorului 5V constant, folosim tehnica PWM (Pulse Width Modulation)
Pornim și oprim curentul foarte rapid. Cu cât pulsul de "ON" este mai lung, cu atât motorul merge mai repede

Monitorizarea: Ecranul OLED primește date prin magistrala I2C și afișează direcția actuală și procentul de viteză aplicat

__Componente folosite__
- Nucleo-L476RG: Calculează PWM-ul și logica direcției

- Driver Motor (Punte H - L293D): Componenta de putere
  Protejează placa STM32 și permite trecerea unui curent mare către motor, având capacitatea de a inversa sensul (pinii IN1 și IN2)

- Motor DC: Motorul de curent continuu pe care îl controlăm

- OLED SSD1306: Afișează în timp real starea sistemului (Sens și Viteză)

- Potențiometru: Pentru a da comenzi fizice de schimbare a vitezei și direcției

__Configurare Hardware (STM32CubeMX)__
Proiectul a fost configurat utilizând următoarea alocare a pinilor:

- PA8 (TIM1_CH1): Generare semnal PWM. Se conectează la pinul de Enable (ENA/ENB) al punții H pentru a dicta viteza.

- PA9 (GPIO_Output): Control logic direcție (se conectează la IN1 pe driver).

- PA10 (GPIO_Output): Control logic direcție (se conectează la IN2 pe driver).

- PB8 (I2C1_SCL) & PB9 (I2C1_SDA): Magistrala de comunicație I2C pentru ecranul OLED.

- Clock (SYSCLK): Configurat la 80 MHz pentru o execuție rapidă și generare precisă a semnalului PWM.

__Logica Codului__
Bucla principală while(1) execută la fiecare 100 ms o secvență fixă de pași:

- citirea valorii ADC prin polling
- calculul direcției și vitezei PWM
- actualizarea pinilor de direcție
- actualizarea registrului CCR1 al timerului
- redesenarea completă a display-ului OLED

__Conversia ADC și PWM__
ADC-ul este configurat pe rezoluție de 12 biți, ceea ce înseamnă că valoarea returnată de HAL_ADC_GetValue() este un număr întreg cuprins între 0 și 4095 (2^12 - 1 = 4095).
Pinul PA8 generează un semnal PWM cu perioada determinată de timer. Rezoluția PWM pe care am ales-o este de 1000 de pași (ARR = 999, deci valori de la 0 la 999 în registrul CCR1). Valoarea 0 în CCR1 înseamnă semnal mereu LOW (motor oprit), valoarea 999 înseamnă semnal mereu HIGH (viteză maximă).

Pentru zona ÎNAINTE (ADC între 2101 și 4095):
PWM = (valoare_ADC - 2100) * 1000 / (4095 - 2100)

Pentru zona ÎNAPOI (ADC între 0 și 1999):
PWM = (1999 - valoare_ADC) * 1000 / 1999

Procentul afișat pe display:
Procent = PWM_calculat * 100 / 1000

__Zona moartă__
Am definit zona moartă între valorile ADC 2000 și 2100. 
Dacă nu aș fi implementat zona moartă, motorul ar fi tremurat haotic când potențiometrul e aproape de centru: valoarea ADC ar fi citit 2045 (zona ÎNAPOI), apoi 2051 (zona ÎNAINTE),motorul ar fi schimbat direcția de zeci de ori pe secundă, producând vibrații mecanice, curenți de pornire repetați care suprasolicită driver-ul și un zgomot deranjant.

__Configurarea ADC__
Am configurat ADC1 cu rezoluție de 12 biți.
   Am ales TIM1
   Clock timer  = 80 MHz
   Frecventa PWM = Clock_timer / (Prescaler + 1) / (ARR + 1)
   Frecventa PWM = 80.000.000 / (79 + 1) / (999 + 1) = 80.000.000 / 80 / 1000 = 1000 Hz = 1 kHz

   



