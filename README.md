# LumaPy

LumaPy to biblioteka dla języka Python służąca do szybkiego prototypowania i tworzenia aplikacji graficznych wykorzystujących API Vulkan, pod maską wykorzystująca C++ dla maksymalnej wydajności.

## Wymagania
- Python 3.10+
- CMake (3.28+)
- Kompilator C++ wspierający standard C++23 (np. MSVC, GCC, Clang)
- Vulkan SDK

## Instalacja

1. Sklonuj repozytorium:
   ```bash
   git clone https://github.com/twoj-profil/lumapy.git
   cd lumapy
   ```

2. Utwórz wirtualne środowisko i aktywuj je:
   ```bash
   python -m venv venv
   # Na systemie Windows:
   .\venv\Scripts\activate
   # Na systemie Linux/macOS:
   source venv/bin/activate
   ```

3. Zainstaluj bibliotekę w trybie developerskim za pomocą `pip`:
   ```bash
   pip install -e .
   ```

## Uruchamianie przykładów

Po pomyślnej instalacji przejdź do folderu `examples/` i uruchom jeden z przygotowanych przykładów, np.:

```bash
cd examples
python test_cube.py
```
