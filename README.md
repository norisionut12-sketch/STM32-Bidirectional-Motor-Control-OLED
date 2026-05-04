# STM32-Bidirectional-Motor-Control-OLED

### Video Proiect
[![Video Proiect](https://img.youtube.com/vi/iiBoR9vVaX8/hqdefault.jpg)](https://youtube.com/shorts/iiBoR9vVaX8)


Sistem de control bidirecțional pentru un motor DC folosind PWM, ADC și un afișaj OLED I2C pe STM32L476RG
1. Ideea de baza este: un potențiometru controlează viteza și direcția unui motor DC, iar un display OLED afișează în permanență starea sistemului. Potențiometrul este singura intrare, iar ieșirile sunt semnalul PWM (care controlează viteza prin duty cycle) și cei doi pini de direcție (care controlează sensul curentului prin puntea H).

2. Componente și Conexiuni:
STM32 Nucleo-L476RG — microcontroller-ul principal
Potențiometru liniar de 10kΩ — configurat ca divizor de tensiune între GND și 3.3V; cursorul din mijloc furnizează tensiunea variabilă citită de ADC
Driver L293D (Punte H) — circuitul integrat care permite controlul bidirecțional al motorului; este necesar deoarece un pin GPIO al STM32 poate furniza maxim câțiva mA, în timp ce un motor DC tipic consumă sute de mA sau chiar amperi
Motor DC de curent continuu (5V)
Display OLED 128x64 pixeli cu controller SSD1306 — conectat prin I2C, folosit pentru afișarea în timp real a vitezei, direcției și valorii brute ADC
Placa Nucleo — furnizează 5V la pinul dedicat pentru alimentarea driver-ului L293D, și 3.3V pentru potențiometru și display

PA0 — intrarea analogică de la cursorul potențiometrului. Tensiunea variază între 0V (potențiometru la capătul stâng) și 3.3V (capătul drept). Am legat un capăt al potențiometrului la GND și celălalt la 3.3V, iar cursorul la PA0.
PA8 — ieșirea PWM generată de TIM1, Channel 1. Acest pin merge direct la pinul Enable (EN) al driver-ului L293D. Duty cycle-ul semnalului PWM de pe acest pin dictează ce procent din tensiunea de alimentare ajunge la motor — cu cât impulsurile sunt mai late, cu atât motorul se rotește mai repede.
PA9 — pin GPIO configurat ca ieșire digitală, legat la IN1 al L293D. Controlează unul din brațele punții H.
PA10 — pin GPIO configurat ca ieșire digitală, legat la IN2 al L293D. Controlează celălalt braț al punții H. Combinația stărilor PA9/PA10 determină direcția curentului prin motor: dacă PA9=HIGH și PA10=LOW, curentul curge într-un sens; dacă PA9=LOW și PA10=HIGH, curentul curge în sens invers; dacă ambii sunt LOW, motorul este oprit (puntea H pune ambele borne ale motorului la același potențial, deci nu circulă curent).
PB8 — linia de ceas SCL a interfeței I2C1, conectată la pinul SCL al display-ului OLED.
PB9 — linia de date SDA a interfeței I2C1, conectată la pinul SDA al display-ului OLED. Display-ul are adresa I2C implicită 0x3C

3. Bucla principală while(1) execută la fiecare 100 ms o secvență fixă de pași:

- citirea valorii ADC prin polling
- calculul direcției și vitezei PWM
- actualizarea pinilor de direcție
- actualizarea registrului CCR1 al timerului
- redesenarea completă a display-ului OLED

4. Conversia ADC și PWM
ADC-ul este configurat pe rezoluție de 12 biți, ceea ce înseamnă că valoarea returnată de HAL_ADC_GetValue() este un număr întreg cuprins între 0 și 4095 (2^12 - 1 = 4095).
Pinul PA8 generează un semnal PWM cu perioada determinată de timer. Rezoluția PWM pe care am ales-o este de 1000 de pași (ARR = 999, deci valori de la 0 la 999 în registrul CCR1). Valoarea 0 în CCR1 înseamnă semnal mereu LOW (motor oprit), valoarea 999 înseamnă semnal mereu HIGH (viteză maximă).

Pentru zona ÎNAINTE (ADC între 2101 și 4095):
PWM = (valoare_ADC - 2100) * 1000 / (4095 - 2100)

Scad 2100 (limita de sus a zonei neutre) din valoarea ADC pentru a obține un interval care pornește de la 0, îl înmulțesc cu rezoluția maximă PWM (1000) și împart la dimensiunea intervalului disponibil (4095 - 2100 = 1995). Rezultatul este o valoare cuprinsă între 0 și 1000, proporțională cu cât de departe e potențiometrul față de centru.

Pentru zona ÎNAPOI (ADC între 0 și 1999):
PWM = (1999 - valoare_ADC) * 1000 / 1999

Aici inversez logica: cu cât potențiometrul e mai spre stânga (valoare ADC mai mică), cu atât viteza în sens invers e mai mare. Scad valoarea ADC din 1999 (limita de jos a zonei neutre minus 1) pentru a obține o valoare care crește când potențiometrul se îndepărtează de centru.
Procentul afișat pe display se calculează simplu:
Procent = PWM_calculat * 100 / 1000

5. Am definit zona moartă între valorile ADC 2000 și 2100. 
Dacă nu aș fi implementat zona moartă, motorul ar fi tremurat haotic când potențiometrul e aproape de centru: valoarea ADC ar fi citit 2045 (zona ÎNAPOI), apoi 2051 (zona ÎNAINTE), apoi din nou 2043, și motorul ar fi schimbat direcția de zeci de ori pe secundă, producând vibrații mecanice, curenți de pornire repetați care suprasolicită driver-ul și un zgomot acustic deranjant.

6. Display-ul SSD1306 de 128x64 pixeli este actualizat complet la fiecare iterație a buclei principale (la fiecare 100 ms)

7. Am configurat ADC1 cu rezoluție de 12 biți.
   Am ales TIM1 (timerul avansat) pentru generarea PWM deoarece pe STM32L476RG canalul TIM1_CH1 este disponibil pe pinul PA8, care nu intră în conflict cu alte funcții
   Clock sistem = 80 MHz (HSI * PLL, PLLM=1, PLLN=10, PLLR=2)
   Clock timer  = 80 MHz (APB2 fără divizare)
   Frecventa PWM = Clock_timer / (Prescaler + 1) / (ARR + 1)
   Frecventa PWM = 80.000.000 / (79 + 1) / (999 + 1) = 80.000.000 / 80 / 1000 = 1000 Hz = 1 kHz

   Am configurat I2C1 în modul Standard (100 kHz), cu adresare pe 7 biți și filtrele analog și digital activate pentru a reduce sensibilitatea la zgomot pe liniile SCL și SDA
   



