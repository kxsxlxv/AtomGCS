# AtomGCS 
## Описание проекта

AtomGCS — Ground Control Station (наземная станция управления) для БПЛА.

## Технический стек
- **Язык:** C++20
- **Библиотека UI:** Dear ImGui (Docking branch) + Графический бэкенд: GLFW + OpenGL3
- **Система сборки:** CMake
- **Библиотека сети:** Asio (Standalone)
- **Математика:** GLM
- **Конфигурация:** nlohmann/json
- **Протокол связи:** UDP/TCP

## Инструкция по установке и сборке проекта

###  1. Клонирование репозитория

Создайте папку `~/src` (если её нет) и перейдите в неё:

```bash
mkdir -p ~/src
cd ~/src
```

Клонируйте репозиторий:
Так как проект использует сторонние библиотеки в виде подмодулей (Git Submodules), используйте флаг --recursive для их автоматической загрузки:

```bash
git clone --recursive <ссылка_на_репозиторий>
```

**Далее можно открыть папку в VS Code ИЛИ собрать и запустить в терминале дальше по инструкции**

---

###  2. Сборка проекта (через терминал)

#### Windows

Создайте папку для сборки и выполните cmake:
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Debug
```

#### Linux

##### Обновление списка пакетов и установка необходимых зависимостей
```bash
sudo apt update

sudo apt install -y \
    build-essential \
    cmake \
    git \
    libx11-dev \
    libxcursor-dev \
    libxinerama-dev \
    libxrandr-dev \
    libxi-dev \
    libgl-dev \
    libmesa-dev \
    libgl1-mesa-dev \
    libcurl4-openssl-dev
```

Создайте папку для сборки и выполните cmake:
```bash
mkdir build
cd build
cmake ..
```

Запустите компиляцию, используя все доступные ядра процессора:

```bash
make -j$(nproc)
```

После успешной сборки в папке `build` появится исполняемый файл `AtomGCS`.

---

#### 3. Запуск программы

#####  3.1. Обычный запуск под Ubuntu

```bash
cd ~/src/AtomGCS/build
./AtomGCS
```
#####  3.2. Запуск в WSL (Ubuntu) 

В WSL графический вывод может работать некорректно. Перед запуском выполните:
```bash
export LIBGL_ALWAYS_SOFTWARE=1
./AtomGCS
```
Это заставит использовать программный рендеринг OpenGL.

---

📂 Структура проекта
 * src/ — исходный код программы.
 * external/ — сторонние библиотеки (ImGui, GLFW, GLM, Asio, json).
 * shared/ — общие заголовочные файлы для протокола обмена данными.
 * resources/ — шейдеры для отрисовки облака точек.
 * tests/ — смоук-тесты и интеграционные тесты.