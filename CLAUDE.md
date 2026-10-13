@AGENTS.md

## Claude Code specifics

- Run `pio.exe` with the PowerShell tool (path in AGENTS.md "Build and flash"); the Bash tool is Git Bash and its
  esptool install fails.
- When the user only says "flash", build and upload to the board that is connected, then report its MAC address.
- Show screen changes as images where possible and look at them before reporting.
