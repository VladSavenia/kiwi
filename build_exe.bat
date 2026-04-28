@echo off
setlocal

python -m pip install --upgrade pip
python -m pip install -r requirements.txt

pyinstaller --noconfirm --clean --onefile --windowed --name OSAL_Code_Generator osal_codegen_app.py

echo.
echo Build completed. EXE is in dist\OSAL_Code_Generator.exe
pause
