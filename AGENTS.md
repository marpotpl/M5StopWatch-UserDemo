# Zasady pracy nad M5StopWatch + Home Assistant

## Projekt i zakres

- Projekt to firmware M5Stack StopWatch na ESP32-S3, bazujący na oficjalnym M5StopWatch-UserDemo.
- Pracujemy na branchu `marcin-ha`. Nie zmieniaj branchy i nie wykonuj merge ani rebase bez wyraźnego polecenia użytkownika.
- Celem jest rozbudowa oryginalnego firmware o bezpośrednią integrację z Home Assistant, bez MQTT.
- Zachowuj oryginalny Watch Face i istniejące aplikacje M5Stack. Nie usuwaj ich ani nie przebudowuj bez wyraźnego polecenia.
- Preferowana architektura nowych funkcji HA to osobne moduły: `ha_wifi`, `ha_time`, w przyszłości `ha_client`, oraz osobne moduły UI dla Home Assistant.
- Docelowo korzystaj z WebSocket API Home Assistant; REST stosuj tam, gdzie ma to uzasadnienie.
- Jeżeli planowana zmiana może naruszyć kompatybilność z oryginalnym firmware M5Stack, najpierw zgłoś to użytkownikowi.

## Sekrety

- Sekrety znajdują się w `main/hal/ha_secrets.h`, który jest ignorowany przez Git. Nigdy nie wyświetlaj, nie kopiuj, nie commituj ani nie ujawniaj jego zawartości.
- Nie umieszczaj haseł, tokenów Home Assistant ani innych sekretów bezpośrednio w plikach śledzonych przez Git.

## Budowanie i weryfikacja

- Projekt używa ESP-IDF 5.5.4. Przed buildem może być konieczne załadowanie środowiska: `. ~/esp/esp-idf/export.sh`.
- Standardowy build: `idf.py build`.
- Po zmianach uruchamiaj build, jeśli jest to możliwe, i samodzielnie poprawiaj błędy kompilacji związane ze swoimi zmianami.
- Nie maskuj istniejących błędów przez wyłączanie warningów ani usuwanie funkcjonalności.
- Nie flashuj urządzenia bez wyraźnego polecenia użytkownika.

## Zasady zmian

- Przed większymi zmianami sprawdzaj `git status`.
- Preferuj małe, czytelne zmiany i zachowuj istniejącą architekturę projektu. Nie modyfikuj plików niezwiązanych z zadaniem.
- Zwracaj szczególną uwagę na rozmiary stosów FreeRTOS. Nie wykonuj ciężkich operacji bezpośrednio w callbackach systemowej pętli zdarzeń ESP-IDF.
- RTC RX8130 przechowuje czas UTC; lokalną strefę czasu obsługuje system/HAL. Polska strefa to CET/CEST z automatyczną zmianą czasu.
- Lokalna modyfikacja `managed_components/78__esp-wifi-connect/wifi_station.cc` przenosi `HandleScanResult()` do tasku `wifi_scan_result`. Jest krytyczna dla stabilności i obecnie nie jest śledzona przez Git. Nie nadpisuj jej ani nie usuwaj.
- Nie wykonuj `git commit` ani `git push` bez wyraźnego polecenia użytkownika.

## Obecny stan

- Wi-Fi, NTP i strefa CET/CEST działają.
- Działa synchronizacja czasu systemowego do RX8130; poprawiono obsługę roku w sterowniku RX8130.
- Rozwiązano stack overflow `sys_evt` podczas skanowania Wi-Fi przez uruchamianie `HandleScanResult()` w tasku `wifi_scan_result`.
- Rozwiązano stack overflow `sys_evt` podczas uruchamiania NTP przez uruchamianie inicjalizacji w osobnym tasku.
- Komunikacja z API Home Assistant nie została jeszcze zaimplementowana.
