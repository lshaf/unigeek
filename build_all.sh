#!/bin/bash
# Build all board environments in parallel and report results

ENVS="m5stickcplus_11 m5stickcplus_2 m5_cardputer m5_cardputer_adv t_lora_pager t_display_16mb diy_smoochie"
JOBS=${1:-4}  # parallel jobs, override with: ./build_all.sh 7
LOGDIR=$(mktemp -d)

echo "Building ${#ENVS[@]} environments with $JOBS parallel jobs..."
echo ""

# Run builds in parallel, limited to $JOBS at a time
PIDS=""
for env in $ENVS; do
  # Wait if we've hit the job limit
  while [ "$(echo "$PIDS" | wc -w)" -ge "$JOBS" ]; do
    NEWPIDS=""
    for pid in $PIDS; do
      if kill -0 "$pid" 2>/dev/null; then
        NEWPIDS="$NEWPIDS $pid"
      fi
    done
    PIDS="$NEWPIDS"
    [ "$(echo "$PIDS" | wc -w)" -ge "$JOBS" ] && sleep 0.5
  done

  # Launch build in background
  (
    pio run -e "$env" > "$LOGDIR/$env.log" 2>&1
    echo $? > "$LOGDIR/$env.exit"
  ) &
  PIDS="$PIDS $!"
  echo "  Started: $env"
done

# Wait for all to finish
wait
echo ""

# Collect results
PASSED=""
FAILED=""
for env in $ENVS; do
  code=$(cat "$LOGDIR/$env.exit")
  if [ "$code" -eq 0 ]; then
    PASSED="$PASSED $env"
  else
    FAILED="$FAILED $env"
  fi
done

# Print summary
echo "======================================"
echo "RESULTS:"
echo "  PASSED:$PASSED"
if [ -n "$FAILED" ]; then
  echo "  FAILED:$FAILED"
  echo ""
  echo "Build logs in: $LOGDIR"
  for env in $FAILED; do
    echo ""
    echo "--- $env (last 20 lines) ---"
    tail -20 "$LOGDIR/$env.log"
  done
  exit 1
else
  echo "  All builds passed!"
  rm -rf "$LOGDIR"
fi