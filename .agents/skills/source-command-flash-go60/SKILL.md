---
name: "source-command-flash-go60"
description: "Migrated source command `flash-go60`"
---

# source-command-flash-go60

Use this skill when the user asks to run the migrated source command `flash-go60`.

## Command Template

Flash Go60 firmware: watch for a Go60 half to enter bootloader via USB and
automatically copy the correct firmware image, reporting each flash in-session.

This needs TWO tools, because neither one alone can do the job:

1. **Bash, backgrounded and unsandboxed** — runs the actual watcher. Writing to
   `/Volumes/GO60*BOOT` is outside the sandbox's write allowlist, so the copy
   fails with EPERM ("could not write ... after 15 attempts") unless
   `dangerouslyDisableSandbox: true` is set. Monitor has no such escape hatch,
   so it cannot run the flasher itself.

   Bash({
     command: "./scripts/flash-go60.sh",
     run_in_background: true,
     dangerouslyDisableSandbox: true,
   })

   Note the output file path it returns.

2. **Monitor, tailing that output file** — turns each result line into a
   session notification. The script loops forever, so as a bare background Bash
   task it never exits and never notifies; flashes land silently and are only
   noticed by manually reading the log. Tailing is a read, which the sandbox
   permits, so this half needs no escape hatch.

   Monitor({
     command: "tail -n +1 -F <output-file-path> 2>&1 | grep -E --line-buffered 'unmounted|ERROR|WARNING|Error:'",
     description: "Go60 flash results",
     persistent: true,
   })

Notes:
- `tail -F` (not `-f`) retries, so it is safe if the file does not exist yet.
- The filter covers success (`... unmounted — flashed.`) *and* the failure
  paths, so silence is never ambiguous. Do not narrow it to success only.
- It matches completion rather than the "detected — flashing" line, so each
  flash produces exactly one notification.
- Stop both with TaskStop when done.

Report each flash to the user as its notification arrives. Keep the watcher
running until the user stops it or every half they intended to flash is done.

Before flashing, check which halves actually changed — compare `shasum -a 256`
of the new `.uf2` files against the previous build. A half whose image is
byte-identical does not need reflashing, which matters because the left half
has no keymap route into its bootloader once the two halves' versions diverge.

Getting a half into bootloader:
- Right half (central): hold RH T3 + `/` (System layer `&bootloader`).
- Left half: only works via the keymap while both halves run matching firmware
  and are talking to each other. Otherwise use MoErgo's power-up method —
  power switch off, USB in, hold outer-left thumb (LH T3) + D, switch on.
  Slow pulsing red LED next to the power switch means it worked.
