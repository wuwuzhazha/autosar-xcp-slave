@echo off
cd /d "%~dp0\..\.."
python Tools\xcpgen\xcpserve.py %*
