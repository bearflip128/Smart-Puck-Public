@echo off
rem Tells the Smart Puck which app is in front (for Sidekick). Leave the window open.
cd /d "%~dp0"
python companion.py %*
