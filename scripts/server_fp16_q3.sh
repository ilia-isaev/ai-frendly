#/bin/bash

#export ZES_ENABLE_SYSMAN=1
#export GGML_SYCL_ENABLE_OPT=0
#export GGML_SYCL_ENABLE_GRAPH=1
#export UR_L0_ENABLE_RELAXED_ALLOCATION_LIMITS=1
#export GGML_SYCL_USM_SYSTEM=1

#QUALITY="high"
#QUALITY="midle"
QUALITY="low"

if   [[ $QUALITY == "high"  ]]; then
	CTXSZ=40960 # total gpu mem: 14.425 GB bf16/bf16 4.36 token/sec
	WATCHDOG_MAX_KV=24576
	QTK=f16
	QTV=f16
	QTXD=f16
elif [[ $QUALITY == "midle" ]]; then
	CTXSZ=81920 # total gpu mem: 15.228 GB q8_0/q8_0
	WATCHDOG_MAX_KV=24576
	QTK=q8_0
	QTV=q8_0
	QTXD=q8_0
else
	CTXSZ=102400 # total gpu mem: 15.088 GB q5_1/q5_1
	WATCHDOG_MAX_KV=24576
	QTK=q5_1
	QTV=q5_1
	QTXD=q5_1
fi

export GGML_SYCL_FA_ONEDNN_MAX_KV=$WATCHDOG_MAX_KV

echo "context cache size: " $CTXSZ " qtk: " $QTK " qtv: " $QTV " qtkvd:" $QTXD
echo "watchdog max kv: " $WATCHDOG_MAX_KV

source ~/intel/oneapi/2026.1/oneapi-vars.sh
#-m model/Qwen3.8-27B-UD-IQ3_XXS.gguf
export LD_LIBRARY_PATH=./lib:~/intel/oneapi/2026.1/lib/
./bin/llama-server \
	-lv 2 \
	--host 127.0.0.1 --port 8089 \
	-m model/Qwen3.8-27B-UD-IQ3_XXS-Kconv.gguf \
	--no-mmproj \
	--spec-type draft-mtp \
	--spec-draft-n-max 2 \
	--draft-p-split 0.30 \
	-cram 0 \
	-ctk $QTK -ctkd $QTXD -ctv $QTV -ctvd $QTXD \
	--ctx-size $CTXSZ \
	-b 512 -ub 512 \
	-np 1 \
	-t 2 \
	-fit on \
	-fa on \
	--jinja \
	--reasoning-effort low \
	--no-context-shift \
	--metrics

