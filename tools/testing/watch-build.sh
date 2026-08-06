#!/usr/bin/env bash
# Emits one line per interesting build-stage change; exits when nix build ends.
LOG=/root/build-minimal.log
last=""
while pgrep -f "nix build" >/dev/null; do
  cur=$(grep -oE "building '/nix/store/[a-z0-9]{32}-[A-Za-z0-9._-]+\.drv'" "$LOG" 2>/dev/null \
        | tail -1 | sed -E "s/^building '\/nix\/store\/[a-z0-9]{32}-//; s/\.drv'$//")
  case "$cur" in
    *openosx*|*openosx*|*apple*|*xnu*|*kc-*|*clang-cross*|*cctools*|*tcc*|*image*)
      if [ -n "$cur" ] && [ "$cur" != "$last" ]; then
        echo "BUILDING: $cur"
        last="$cur"
      fi
      ;;
  esac
  sleep 60
done
if [ -e /root/openosx/result ]; then
  echo "BUILD_SUCCEEDED: $(ls /root/openosx/result/ 2>/dev/null | tr '\n' ' ')"
else
  echo "BUILD_ENDED_NO_RESULT: $(grep -iE 'error:|failed' "$LOG" | tail -3 | cut -c1-250 | tr '\n' '|')"
fi
