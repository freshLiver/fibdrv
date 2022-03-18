CPU_ID=11

MODE=0
REPEAT=1000
RANGE_FROM=0
RANGE_TO=92
LOG_FILE="log-m${MODE}_${RANGE_FROM}-${RANGE_TO}_r${REPEAT}.json"


echo "DISABLE ASLR"
sudo sh -c "echo 0 > /proc/sys/kernel/randomize_va_space"


echo "DISABLE SMP IRQ"
sudo sh -c "echo 7ff > /proc/irq/default_smp_affinity"

echo "DISABLE INTEL TURBO"
sudo sh -c "echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo"


echo "SET CPU$CPU_ID TO PERFORMANCE MODE"
CPU_MODE_OLD="$(cat /sys/devices/system/cpu/cpu${CPU_ID}/cpufreq/scaling_governor)"
sudo sh -c "echo performance > /sys/devices/system/cpu/cpu$CPU_ID/cpufreq/scaling_governor"


echo "CLEAR LOG_FILE CONTENT"
echo -n "" > "$LOG_FILE"


echo "START MEASURING..."
echo "{" >> "$LOG_FILE"
for TARGET in $(seq $RANGE_FROM $RANGE_TO); do
    echo "MEASURING FIB($TARGET)..."
    echo "    \"$TARGET\" : [" >> "$LOG_FILE"
    for i in $(seq 1 $REPEAT); do 
        taskset -c $CPU_ID ./read_perf.out $TARGET $MODE >> $LOG_FILE;
        if [ "$i" = "$REPEAT" ]; then
            echo "" >> "$LOG_FILE"
        else
            echo "," >> "$LOG_FILE"
        fi
    done
    if [ "$TARGET" = "$RANGE_TO" ]; then
        echo "    ]" >> "$LOG_FILE"
    else 
        echo "    ]," >> "$LOG_FILE"
    fi
done
echo "}" >> "$LOG_FILE"
echo "STOP MEASURING..."

echo "RECOVER CPU MODE TO $CPU_MODE_OLD"
sudo sh -c "echo $CPU_MODE_OLD > /sys/devices/system/cpu/cpu$CPU_ID/cpufreq/scaling_governor"