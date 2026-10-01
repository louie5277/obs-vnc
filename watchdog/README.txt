OBS Watchdog - WM_NULL
=======================

This is a native Windows EXE. The OBS PC does not need Python.

Detection:
- Every 10 seconds, check whether obs64.exe exists.
- If it exists, find a visible top-level window belonging to that process.
- Send the harmless WM_NULL Windows message with a 3-second timeout.
- Require 2 consecutive failures by default (~20 seconds) before declaring
  OBS not responding.
- Send a Teams/Power Automate HTTP alert.
- Send a recovery notification when OBS responds again.
- Log to obs_watchdog.log.

No user action is required for WM_NULL. It is a no-op Windows message.

Keep AutoRestartOBS=0 initially.

GitHub build:
1. Put watchdog/obs_watchdog.cpp and watchdog/config.ini into your repo.
2. Put the workflow under .github/workflows/.
3. Run Actions -> Build OBS Watchdog - WM_NULL.
4. Download OBSWatchdog-WMNULL-x64.
5. Put OBSWatchdog.exe and config.ini in the same folder on the OBS PC.
6. Set TeamsUrl in config.ini.

Task Scheduler:
- Trigger: At log on.
- Program: OBSWatchdog.exe
- Run only when user is logged on.
- Start in: folder containing OBSWatchdog.exe.
