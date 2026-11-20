#!/bin/sh
# One-time Pi provisioning for the F429 stream gateway (Kali aarch64).
set -eu
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y ffmpeg
mkdir -p /home/pi/f429cam/record /home/pi/f429cam/run
if [ ! -x /home/pi/f429cam/mediamtx ]; then
	curl -sSL -o /tmp/mediamtx.tgz \
		https://github.com/bluenviron/mediamtx/releases/download/v1.21.4/mediamtx_v1.21.4_linux_arm64.tar.gz
	tar xzf /tmp/mediamtx.tgz -C /home/pi/f429cam
	rm -f /tmp/mediamtx.tgz
fi
ffmpeg -hide_banner -encoders 2>/dev/null | grep -E 'libx264|aac ' | head -3
/home/pi/f429cam/mediamtx --version
