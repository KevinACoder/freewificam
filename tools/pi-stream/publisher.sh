#!/bin/sh
# Feed the receiver's FIFOs to mediamtx as H.264/AAC RTSP.
# usage: publisher.sh video|av <video.fifo> [audio.fifo]
set -eu
MODE=${1:?video|av}
VIDEO=${2:?video fifo}
AUDIO=${3:-}
RTSP=${RTSP:-rtsp://127.0.0.1:8554/cam}
LOG=${LOG:-/home/pi/f429cam/publisher.log}

while :; do
	if [ "$MODE" = av ]; then
		ffmpeg -hide_banner -loglevel warning -nostdin \
			-use_wallclock_as_timestamps 1 -f mjpeg -framerate 14 -i "$VIDEO" \
			-use_wallclock_as_timestamps 1 -f s16le -ar 16000 -ac 1 -i "$AUDIO" \
			-c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p \
			-g 28 -b:v 1500k -c:a aac -b:a 32k -ar 16000 -ac 1 \
			-f rtsp -rtsp_transport tcp "$RTSP" >>"$LOG" 2>&1
	else
		ffmpeg -hide_banner -loglevel warning -nostdin \
			-use_wallclock_as_timestamps 1 -f mjpeg -framerate 14 -i "$VIDEO" \
			-c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p \
			-g 28 -b:v 1500k \
			-f rtsp -rtsp_transport tcp "$RTSP" >>"$LOG" 2>&1
	fi
	sleep 1
done
