# OSAL Code Generator (Python GUI)

Приложение генерирует C-файлы OSAL для embedded-проектов на основе шаблонов в `osal_templates`.

## Возможности

- GUI (Tkinter) с аккуратным тёмным интерфейсом.
- Настройка префикса модуля (замена `sys_param_srv` / `SysParamSrv` / `sysParamSrv` / `SYS_PARAM_SRV`).
- Выбор набора API:
  - Queues
  - Locks
  - Threads
  - Time
  - Memory
- Выбор порта:
  - `freertos` — поддерживается сейчас
  - `posix (coming soon)` — предусмотрено на будущее
- Генерация структуры:
  - `<prefix>/...`
  - `<prefix>/portable/...`
  - `<prefix>/<prefix>_osal_profile.h`

## Запуск из исходников

```bash
python osal_codegen_app.py
```

## Сборка `.exe`

### Вариант 1 (Windows)

Запустить `build_exe.bat`.

### Вариант 2 (вручную)

```bash
python -m pip install -r requirements.txt
pyinstaller --noconfirm --clean --onefile --windowed --name OSAL_Code_Generator --add-data "osal_templates;osal_templates" osal_codegen_app.py
```

Готовый файл будет в `dist/OSAL_Code_Generator.exe`.

> Важно: шаблоны `osal_templates` вшиваются в `.exe`, поэтому приложение корректно работает без ошибки `templates directory not found`.

## Как работает ограничение API

Для отключённых групп API генератор физически вырезает код из генерируемых файлов:
- нет прототипов в `<prefix>_osal.h`
- нет полей в vtable/ptable
- нет реализаций функций в `portable/*_freertos.c`

Это позволяет формировать минимальный профиль, где, например, остаются только lock-механизмы.
