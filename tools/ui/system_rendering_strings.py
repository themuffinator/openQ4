#!/usr/bin/env python3
"""Localized SYSTEM rendering controls (#str_230141 through #str_230156)."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
TEXT = {
    'english': [
        'Rendering Backend', 'Rendering Device', 'Automatic;OpenGL;Vulkan;OpenGL ES',
        'Automatic (high performance)',
        'Automatic prefers compatible high-performance hardware. Choosing a GPU uses Vulkan.',
        'OpenGL uses system GPU routing. Choosing a GPU switches to Vulkan.',
        'Apply your selection, then restart the renderer before using Auto-Detect.',
        'Restart the renderer to use your saved selection.',
        'Return to the main menu to restart with your saved selection.',
        'The saved GPU is unavailable. Choose Automatic or another device.',
        'A diagnostic GPU override is active. Restart without r_vkDevice to select a device here.',
        'Active renderer', 'Restart Renderer', 'Unavailable device',
        'Rendering', 'The requested renderer is unavailable. The active fallback is shown above.'],
    'french': [
        'Moteur de rendu', 'Périphérique de rendu', 'Automatique;OpenGL;Vulkan;OpenGL ES',
        'Automatique (hautes performances)',
        'Le mode automatique privilégie le matériel compatible performant. Choisir un GPU utilise Vulkan.',
        'OpenGL utilise le GPU choisi par le système. Choisir un GPU active Vulkan.',
        'Appliquez votre choix et redémarrez le rendu avant la détection automatique.',
        'Redémarrez le rendu pour utiliser votre choix enregistré.',
        'Revenez au menu principal pour redémarrer avec votre choix enregistré.',
        'Le GPU enregistré est indisponible. Choisissez Automatique ou un autre périphérique.',
        'Un GPU de diagnostic est imposé. Redémarrez sans r_vkDevice pour choisir un périphérique ici.',
        'Rendu actif', 'Redémarrer le rendu', 'Périphérique indisponible',
        'Rendu', 'Le rendu demandé est indisponible. Le rendu de secours actif est indiqué ci-dessus.'],
    'german': [
        'Grafikschnittstelle', 'Grafikgerät', 'Automatisch;OpenGL;Vulkan;OpenGL ES',
        'Automatisch (hohe Leistung)',
        'Automatisch bevorzugt kompatible, leistungsstarke Hardware. Eine GPU-Auswahl verwendet Vulkan.',
        'OpenGL nutzt die GPU-Zuweisung des Systems. Eine GPU-Auswahl wechselt zu Vulkan.',
        'Auswahl anwenden und den Renderer vor der automatischen Erkennung neu starten.',
        'Renderer neu starten, um die gespeicherte Auswahl zu verwenden.',
        'Zum Hauptmenü zurückkehren, um mit der gespeicherten Auswahl neu zu starten.',
        'Die gespeicherte GPU ist nicht verfügbar. Automatisch oder ein anderes Gerät wählen.',
        'Eine Diagnose-GPU ist vorgegeben. Ohne r_vkDevice neu starten, um hier ein Gerät zu wählen.',
        'Aktiver Renderer', 'Renderer neu starten', 'Gerät nicht verfügbar',
        'Grafik', 'Der gewünschte Renderer ist nicht verfügbar. Der aktive Ersatz wird oben angezeigt.'],
    'italian': [
        'API di rendering', 'Dispositivo di rendering', 'Automatico;OpenGL;Vulkan;OpenGL ES',
        'Automatico (alte prestazioni)',
        'Automatico preferisce hardware compatibile ad alte prestazioni. Scegliere una GPU usa Vulkan.',
        'OpenGL usa la GPU scelta dal sistema. Scegliere una GPU attiva Vulkan.',
        'Applica la scelta e riavvia il renderer prima del rilevamento automatico.',
        'Riavvia il renderer per usare la scelta salvata.',
        'Torna al menu principale per riavviare con la scelta salvata.',
        'La GPU salvata non è disponibile. Scegli Automatico o un altro dispositivo.',
        'È attiva una GPU di diagnostica. Riavvia senza r_vkDevice per scegliere un dispositivo qui.',
        'Renderer attivo', 'Riavvia renderer', 'Dispositivo non disponibile',
        'Rendering', 'Il renderer richiesto non è disponibile. Quello di riserva attivo è indicato sopra.'],
    'spanish': [
        'API de renderizado', 'Dispositivo de renderizado', 'Automático;OpenGL;Vulkan;OpenGL ES',
        'Automático (alto rendimiento)',
        'Automático prioriza hardware compatible de alto rendimiento. Elegir una GPU usa Vulkan.',
        'OpenGL usa la GPU asignada por el sistema. Elegir una GPU activa Vulkan.',
        'Aplica la selección y reinicia el renderizador antes de la detección automática.',
        'Reinicia el renderizador para usar la selección guardada.',
        'Vuelve al menú principal para reiniciar con la selección guardada.',
        'La GPU guardada no está disponible. Elige Automático u otro dispositivo.',
        'Hay una GPU de diagnóstico forzada. Reinicia sin r_vkDevice para elegir un dispositivo aquí.',
        'Renderizador activo', 'Reiniciar renderizador', 'Dispositivo no disponible',
        'Renderizado', 'El renderizador solicitado no está disponible. El alternativo activo se muestra arriba.'],
    'brazilian': [
        'API de renderização', 'Dispositivo de renderização', 'Automático;OpenGL;Vulkan;OpenGL ES',
        'Automático (alto desempenho)',
        'Automático prioriza hardware compatível de alto desempenho. Escolher uma GPU usa Vulkan.',
        'OpenGL usa a GPU definida pelo sistema. Escolher uma GPU ativa Vulkan.',
        'Aplique a seleção e reinicie o renderizador antes da detecção automática.',
        'Reinicie o renderizador para usar a seleção salva.',
        'Volte ao menu principal para reiniciar com a seleção salva.',
        'A GPU salva está indisponível. Escolha Automático ou outro dispositivo.',
        'Uma GPU de diagnóstico foi forçada. Reinicie sem r_vkDevice para escolher um dispositivo aqui.',
        'Renderizador ativo', 'Reiniciar renderizador', 'Dispositivo indisponível',
        'Renderização', 'O renderizador solicitado está indisponível. O substituto ativo aparece acima.'],
    'polish': [
        'Interfejs renderowania', 'Urządzenie renderujące', 'Automatycznie;OpenGL;Vulkan;OpenGL ES',
        'Automatycznie (wysoka wydajność)',
        'Tryb automatyczny preferuje zgodny sprzęt o wysokiej wydajności. Wybór GPU włącza Vulkan.',
        'OpenGL używa GPU wybranego przez system. Wybór GPU przełącza na Vulkan.',
        'Zastosuj wybór i uruchom ponownie renderer przed automatycznym wykrywaniem.',
        'Uruchom ponownie renderer, aby użyć zapisanego wyboru.',
        'Wróć do menu głównego, aby uruchomić ponownie z zapisanym wyborem.',
        'Zapisane GPU jest niedostępne. Wybierz Automatycznie lub inne urządzenie.',
        'Wymuszono GPU do diagnostyki. Uruchom ponownie bez r_vkDevice, aby wybrać urządzenie tutaj.',
        'Aktywny renderer', 'Uruchom renderer ponownie', 'Urządzenie niedostępne',
        'Renderowanie', 'Żądany renderer jest niedostępny. Aktywny renderer zastępczy jest widoczny powyżej.'],
    'czech': [
        'Vykreslovací rozhraní', 'Vykreslovací zařízení', 'Automaticky;OpenGL;Vulkan;OpenGL ES',
        'Automaticky (vysoký výkon)',
        'Automatický režim upřednostňuje kompatibilní výkonný hardware. Výběr GPU používá Vulkan.',
        'OpenGL používá GPU zvolené systémem. Výběr GPU přepne na Vulkan.',
        'Použijte výběr a před automatickou detekcí restartujte vykreslování.',
        'Pro použití uloženého výběru restartujte vykreslování.',
        'Vraťte se do hlavní nabídky a restartujte s uloženým výběrem.',
        'Uložené GPU není dostupné. Vyberte Automaticky nebo jiné zařízení.',
        'Je vynuceno diagnostické GPU. Pro výběr zařízení restartujte bez r_vkDevice.',
        'Aktivní vykreslování', 'Restartovat vykreslování', 'Zařízení není dostupné',
        'Vykreslování', 'Požadovaný renderer není dostupný. Aktivní náhradní renderer je uveden výše.'],
    'hungarian': [
        'Renderelési háttérrendszer', 'Renderelő eszköz', 'Automatikus;OpenGL;Vulkan;OpenGL ES',
        'Automatikus (nagy teljesítmény)',
        'Az automatikus mód kompatibilis, nagy teljesítményű hardvert választ. A GPU kiválasztása Vulkant használ.',
        'Az OpenGL a rendszer által választott GPU-t használja. A GPU kiválasztása Vulkanra vált.',
        'Alkalmazd a választást, majd indítsd újra a renderelőt az automatikus felismerés előtt.',
        'Indítsd újra a renderelőt a mentett választás használatához.',
        'Térj vissza a főmenübe a mentett választással történő újraindításhoz.',
        'A mentett GPU nem érhető el. Válaszd az Automatikus módot vagy egy másik eszközt.',
        'Diagnosztikai GPU van kényszerítve. Indítsd újra r_vkDevice nélkül az eszközválasztáshoz.',
        'Aktív renderelő', 'Renderelő újraindítása', 'Nem elérhető eszköz',
        'Renderelés', 'A kért renderelő nem érhető el. Az aktív tartalék renderelő fent látható.'],
    'turkish': [
        'İşleme arka ucu', 'İşleme aygıtı', 'Otomatik;OpenGL;Vulkan;OpenGL ES',
        'Otomatik (yüksek performans)',
        'Otomatik, uyumlu yüksek performanslı donanımı tercih eder. GPU seçimi Vulkan kullanır.',
        'OpenGL sistemin seçtiği GPU’yu kullanır. GPU seçmek Vulkan’a geçer.',
        'Seçimi uygulayın ve otomatik algılamadan önce işleyiciyi yeniden başlatın.',
        'Kaydedilen seçimi kullanmak için işleyiciyi yeniden başlatın.',
        'Kaydedilen seçimle yeniden başlatmak için ana menüye dönün.',
        'Kaydedilen GPU kullanılamıyor. Otomatik veya başka bir aygıt seçin.',
        'Tanılama GPU’su zorlanıyor. Burada aygıt seçmek için r_vkDevice olmadan yeniden başlatın.',
        'Etkin işleyici', 'İşleyiciyi yeniden başlat', 'Aygıt kullanılamıyor',
        'İşleme', 'İstenen işleyici kullanılamıyor. Etkin yedek işleyici yukarıda gösteriliyor.'],
    'russian': [
        'Графический API', 'Устройство рендеринга', 'Автоматически;OpenGL;Vulkan;OpenGL ES',
        'Автоматически (высокая производительность)',
        'Автовыбор предпочитает совместимое высокопроизводительное оборудование. Выбор GPU включает Vulkan.',
        'OpenGL использует GPU, назначенный системой. Выбор GPU переключает на Vulkan.',
        'Примените выбор и перезапустите рендерер перед автоопределением качества.',
        'Перезапустите рендерер, чтобы использовать сохранённый выбор.',
        'Вернитесь в главное меню для перезапуска с сохранённым выбором.',
        'Сохранённый GPU недоступен. Выберите автоматический режим или другое устройство.',
        'Задан диагностический GPU. Перезапустите без r_vkDevice, чтобы выбрать устройство здесь.',
        'Активный рендерер', 'Перезапустить рендерер', 'Устройство недоступно',
        'Рендеринг', 'Запрошенный рендерер недоступен. Активный резервный вариант показан выше.'],
    'ukrainian': [
        'Графічний API', 'Пристрій рендерингу', 'Автоматично;OpenGL;Vulkan;OpenGL ES',
        'Автоматично (висока продуктивність)',
        'Автовибір надає перевагу сумісному високопродуктивному обладнанню. Вибір GPU вмикає Vulkan.',
        'OpenGL використовує GPU, призначений системою. Вибір GPU перемикає на Vulkan.',
        'Застосуйте вибір і перезапустіть рендерер перед автовизначенням якості.',
        'Перезапустіть рендерер, щоб використати збережений вибір.',
        'Поверніться до головного меню для перезапуску зі збереженим вибором.',
        'Збережений GPU недоступний. Виберіть автоматичний режим або інший пристрій.',
        'Задано діагностичний GPU. Перезапустіть без r_vkDevice, щоб вибрати пристрій тут.',
        'Активний рендерер', 'Перезапустити рендерер', 'Пристрій недоступний',
        'Рендеринг', 'Запитаний рендерер недоступний. Активний резервний варіант показано вище.'],
}


def main():
    for locale, entries in TEXT.items():
        assert len(entries) == 16
        path = ROOT / 'content/baseoq4/pak0/strings' / (locale + '_openq4.lang')
        source = path.read_text(encoding='utf-8')
        for i, value in enumerate(entries, 230141):
            key = '#str_' + str(i)
            line = '\t"' + key + '"\t"' + value + '"'
            pattern = r'(?m)^\s*"' + key + r'"\s+"[^"\r\n]*"\s*$'
            if re.search(pattern, source): source = re.sub(pattern, lambda _: line, source)
            else:
                end = source.rfind('}')
                source = source[:end] + line + '\n' + source[end:]
        path.write_text(source, encoding='utf-8', newline='\n')


if __name__ == '__main__': main()
