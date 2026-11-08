# Kali Raspberry Pi 4B AP Bench

This document describes the standard controlled-AP host for the wireless bench:
a Raspberry Pi 4B running Kali Linux from an encrypted SD card. It is the
reference for rebuilding the card and for operating the AP, the monitor card,
and the capture card.

Status: standard bench revision 2026-11-08. One item is still open: the AP data
path has not yet been validated with a real station (see section 8).

## 1. Roles

| Interface | Hardware | Role |
|---|---|---|
| `wlan2` | RTL8812AU (USB, `0bda:8812`, MAC `1c:a7:70:de:29:a1`) | controlled AP, 2.4 GHz |
| `wlan1` | MT7921AU (USB, `0e8d:7961`, MAC `90:de:80:4c:38:c5`) | monitor (passive) |
| `wlan0` | BCM43455 (onboard, `brcmfmac`) | retired from AP duty; not used by the bench |
| `eth0` | onboard Gigabit Ethernet | management, `192.168.0.17/24` |

The AP and the monitor must be different radios. The RTL8812AU is the only card
on this host that is known to do AP, monitor, and injection at the same time;
the MT7921AU does monitor but not injection, and its 2.4 GHz AP mode does not
emit beacons on this kernel (section 9).

The bench station is a separate RK3568 board. It is not described here.

## 2. Base system

- Hardware: Raspberry Pi 4 Model B Rev 1.5, 8 GB RAM.
- OS: Kali GNU/Linux Rolling 2026.3, arm64, kernel `6.12.34+rpt-rpi-v8`.
- Root filesystem: LUKS2 container on the second SD partition, ext4 inside,
  label `kali-root`. Cipher `aes-xts-plain64`, 512-bit key, argon2id.
- Unlock: the initramfs opens the container with a 4096-byte key file
  (`/etc/luks.key`, mode 0400). A second key slot holds a recovery passphrase.
  The key file is embedded in the initramfs, so the card boots unattended; it
  also means that anyone who can read the unencrypted boot partition can
  decrypt the card. This is accepted for a bench host that is kept physically
  controlled.
- Boot firmware: `/boot/firmware/cmdline.txt` uses
  `root=/dev/mapper/crypt cryptdevice=PARTUUID=...:crypt`; `config.txt` has
  `arm_64bit=1` and `initramfs initramfs.gz followkernel`.
- Kernel updates: `/etc/initramfs/post-update.d/zz-fde-initramfs` copies the
  freshly built initramfs for `rpt-rpi-v8` to
  `/boot/firmware/initramfs.gz`, so the LUKS initramfs stays in sync with
  `initramfs8`.
- Users: `pi` / `pi`, uid 1000, `sudo` without a password. `root` is locked.
- `sshd` is enabled and accepts password authentication.
- `cloud-init` is disabled (`/etc/cloud/cloud-init.disabled`), so it cannot run
  its `growpart`/`resizefs` modules against the encrypted root.
- The image ships an ifupdown stanza for `eth0` (DHCP). It is moved aside to
  `/root/eth0-ifupdown.disabled` and NetworkManager owns `eth0`, so the static
  profile is not overridden.
- The root filesystem fills the 58.2 GB card; about 14 GB is used.

## 3. Network

| Role | Address | Notes |
|---|---|---|
| management (`eth0`) | `192.168.0.17/24`, gateway/DNS `192.168.0.1` | NetworkManager profile `eth0-static`, priority 100 |
| AP (`wlan2`) | `192.168.99.1/24` | added by the AP service |
| AP DHCP | `192.168.99.10` - `192.168.99.100` | dnsmasq, optional for the bench |
| bench station | `192.168.99.2/24` | static on the station, not DHCP |

The host name is `kali-raspberrypi`.

## 4. Packages

The image already contains `iw`, `dnsmasq-base`, `aircrack-ng`, `tcpdump`,
`tshark`, `dumpcap`, `nmap`, and the kernel headers for `6.12.34+rpt-rpi-v8`
plus `dkms` and `gcc-12`.

Install the AP and load tools:

```sh
apt install hostapd iperf3
```

`hostapd` is not enabled as a system service; the bench unit in section 5 owns
the AP. The distribution package disables its init script automatically.

The RTL8812AU driver is a DKMS package from the Kali contrib component:

```sh
apt install realtek-rtl88xxau-dkms
```

This builds `88XXau.ko` (aircrack-ng driver, version 5.6.4.2) for every
installed kernel. The module name is `88XXau`; its USB alias covers
`0bda:8812`, so udev loads it at boot. The in-tree kernel `6.12.34` does not
contain `rtw88_8812au`, so without this package the card enumerates but gets
no network interface.

## 5. AP service

The AP is a small systemd unit with two helper scripts and two configuration
files.

### 5.1 `lab-ap.service`

```ini
[Unit]
Description=Lab AP (8812au wlan2 / labwifi-ar-ht20s / 192.168.99.1)
After=network.target
Wants=network.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/lab-ap-up.sh
ExecStop=/usr/local/sbin/lab-ap-down.sh

[Install]
WantedBy=multi-user.target
```

### 5.2 `/etc/lab-ap/hostapd.conf`

```ini
interface=wlan2
driver=nl80211
ssid=labwifi-ar-ht20s
hw_mode=g
channel=5
ieee80211n=1
ht_capab=[SHORT-GI-20]
wmm_enabled=1
auth_algs=1
ignore_broadcast_ssid=0
ctrl_interface=/run/lab-ap/hostapd
```

The AP is open (no WPA) because the bench station joins it with a static
address and the test traffic is already isolated on `192.168.99.0/24`.

### 5.3 `/etc/lab-ap/dnsmasq.conf`

```ini
interface=wlan2
bind-interfaces
domain-needed
bogus-priv
dhcp-range=192.168.99.10,192.168.99.100,255.255.255.0,12h
dhcp-option=3,192.168.99.1
dhcp-option=6,192.168.0.1
log-facility=/var/log/lab-ap/dnsmasq.log
```

`bind-interfaces` plus `interface=wlan2` keeps this instance off `eth0` and off
the system resolver.

### 5.4 `/usr/local/sbin/lab-ap-up.sh`

```sh
#!/bin/bash
set -u
IF=wlan2
AP_MAC=1c:a7:70:de:29:a1
CONF=/etc/lab-ap/hostapd.conf
LOG=/var/log/lab-ap
mkdir -p "$LOG" /run/lab-ap
for i in $(seq 1 60); do [ -e /sys/class/net/$IF ] && break; sleep 0.5; done
[ -e /sys/class/net/$IF ] || { echo "no $IF"; exit 1; }
PERM=$(ethtool -P "$IF" 2>/dev/null | awk "{print \$3}")
if [ -n "$PERM" ] && [ "$PERM" != "$AP_MAC" ]; then
  echo "$IF is $PERM, expected $AP_MAC (8812au)"; exit 1
fi
nmcli device set $IF managed no 2>/dev/null || true
pkill -x hostapd 2>/dev/null || true
pkill -f "dnsmasq.*--conf-file=/etc/lab-ap/dnsmasq.conf" 2>/dev/null || true
sleep 1
ip link set $IF down 2>/dev/null || true
ip link set $IF up
hostapd -B -P /run/lab-ap/hostapd.pid -f "$LOG/hostapd.log" "$CONF" || exit 1
ok=0
for i in $(seq 1 60); do
  iw dev $IF info 2>/dev/null | grep -q "type AP" && { ok=1; break; }
  sleep 0.25
done
[ "$ok" = 1 ] || { echo "AP did not come up"; tail -5 "$LOG/hostapd.log"; exit 1; }
ip addr replace 192.168.99.1/24 dev $IF
dnsmasq --conf-file=/etc/lab-ap/dnsmasq.conf --pid-file=/run/lab-ap/dnsmasq.pid
echo "lab-ap up: $(iw dev $IF info | sed -n "s/^\tssid //p")"
```

### 5.5 `/usr/local/sbin/lab-ap-down.sh`

```sh
#!/bin/bash
pkill -F /run/lab-ap/hostapd.pid 2>/dev/null || true
pkill -F /run/lab-ap/dnsmasq.pid 2>/dev/null || true
ip addr del 192.168.99.1/24 dev wlan2 2>/dev/null || true
nmcli device set wlan2 managed yes 2>/dev/null || true
```

### 5.6 Install and operate

```sh
chmod 0755 /usr/local/sbin/lab-ap-up.sh /usr/local/sbin/lab-ap-down.sh
systemctl daemon-reload
systemctl enable --now lab-ap
systemctl status lab-ap
iw dev wlan2 info
ip -4 addr show wlan2
```

Logs: `/var/log/lab-ap/hostapd.log`, `/var/log/lab-ap/dnsmasq.log`. The unit is
`RemainAfterExit=yes` runners: `hostapd` and `dnsmasq` are separate daemons
started by the script and stopped by `lab-ap-down.sh`.

## 6. Interface names

The USB enumeration order is not stable: after one reboot the two USB cards
swapped `wlan1` and `wlan2`, and the AP came up on the MT7921AU, which does not
emit beacons on 2.4 GHz. The bench pins the names by permanent MAC with
systemd `.link` files:

```ini
/etc/systemd/network/10-lab-wlan2.link
[Match]
MACAddress=1c:a7:70:de:29:a1
[Link]
Name=wlan2

/etc/systemd/network/10-lab-wlan1.link
[Match]
MACAddress=90:de:80:4c:38:c5
[Link]
Name=wlan1

/etc/systemd/network/10-lab-wlan0.link
[Match]
MACAddress=d8:3a:dd:26:91:1e
[Link]
Name=wlan0
```

`wlan2` is the RTL8812AU (AP), `wlan1` is the MT7921AU (monitor), and `wlan0`
is the onboard BCM43455. The AP service also checks the permanent address, so
a wrong mapping fails loudly instead of starting the AP on the MTK card:

```sh
ethtool -P wlan2        # must print 1c:a7:70:de:29:a1
```

After adding the rules, reboot once and re-check `iw dev` and the beacon
capture.

## 7. Operating the bench

AP up (also runs at boot):

```sh
systemctl start lab-ap
```

AP down, for example to use the RTL8812AU for a capture:

```sh
systemctl stop lab-ap
```

Monitor on the MT7921AU while the AP runs:

```sh
ip link set wlan1 down
iw dev wlan1 set type monitor
ip link set wlan1 up
iw dev wlan1 set channel 5
tcpdump -i wlan1 -w capture.pcap
```

Capture or inject with the RTL8812AU (AP must be stopped first, because the AP
owns `wlan2`):

```sh
systemctl stop lab-ap
ip link set wlan2 down
iw dev wlan2 set type monitor
ip link set wlan2 up
iw dev wlan2 set channel 1
aireplay-ng --test -a <bssid> -e <ssid> wlan2
```

Return both cards to managed mode when done, then start the AP again:

```sh
iw dev wlan2 set type managed
iw dev wlan1 set type managed
systemctl start lab-ap
```

The RTL8812AU can serve as a 5 GHz AP too (`hw_mode=a`, `channel=36`), which is
useful when a test needs a 5 GHz AP while the MT7921AU stays on monitor.

## 8. Verification

Verified on 2026-11-08:

- The card boots with the encrypted root unlocked by the key file; `findmnt /`
  reports `/dev/mapper/crypt` and `ext4`.
- `eth0` comes up at `192.168.0.17/24`; `ssh pi@192.168.0.17` accepts `pi/pi`.
- The RTL8812AU driver is built and installed by DKMS; `88XXau` loads at boot;
  `wlan2` appears with 20 dBm reported transmit power.
- `lab-ap` starts at boot; `wlan2` is type AP, SSID `labwifi-ar-ht20s`,
  channel 5, width 20 MHz, address `192.168.99.1/24`.
- Beacons are visible on the MT7921AU monitor on channel 5.
- `hostapd` logs `AP-ENABLED`; `dnsmasq` listens on `192.168.99.1:53` and UDP
  port 67 bound to `wlan2`.
- RTL8812AU monitor capture: 30 beacons in a 12 s capture, radiotap signal
  present.
- RTL8812AU injection: `aireplay-ng --test` reports `Injection is working`
  and 30/30 directed probe responses (100 %, about -29 dBm).
- The onboard `brcmfmac` associates to the AP (AP station table shows it,
  signal about -26 dBm), but does not pass data with its nexmon firmware.
- The MT7921AU station association is unreliable: it associates once in a
  while and then drops; repeated clean attempts time out.
- The bench station (RK3568) boots the mfs image from this card over TFTP,
  mounts the NFS root, loads the RTL8851BU with `modprobe rtw89_8851bu`
  (firmware from the initramfs), associates to `labwifi-ar-ht20s`, and pings
  `192.168.99.1` 3/3 with zero loss. The station link reports -20 dBm and
  MCS6 58.5 Mbit/s; the AP station table shows the station at -27 dBm.

The data path is verified with the real station. The onboard `brcmfmac` and
the MT7921AU remain unsuitable as stations, as listed above; use the RK3568
for the station side. A DHCP lease was not exercised because the station uses
the static `192.168.99.2`; `dnsmasq` is configured and listens for optional
clients.

## 9. Known issues

- MT7921AU 2.4 GHz AP mode does not emit beacons on `6.12.34+rpt-rpi-v8`.
  `hostapd` reaches `AP-ENABLED` and the driver accepts the beacon, but neither
  a second radio nor the onboard radio sees the beacon on the air. The same
  card does emit beacons as a 5 GHz AP, so the failure is specific to the
  2.4 GHz AP path on this kernel.
- MT7921AU station mode on 2.4 GHz is unreliable (association timeouts, or an
  association that drops immediately). Treat the MTK card as a monitor, or as
  a 5 GHz AP, not as a 2.4 GHz station.
- MT7921AU injection does not work: 30 directed probe requests from its monitor
  interface produced zero responses.
- `iw dev wlan1` reports `txpower 3.00 dBm` and manual `set txpower` is
  accepted but ignored. This is a known mt76 reporting bug: `txpower_cur` is
  never set, so the reported value is `0 + antenna gain / 2 = 3`. It does not
  mean the radio transmits at 3 dBm; the 5 GHz AP beacons are received at
  17 dBm. The fix series ("wifi: mt76: fix returned txpower for mt7921 and
  mt7925", February 2025) is not in `6.12.34`.
- The onboard `brcmfmac` runs the nexmon firmware, which is aimed at monitor
  and injection. As a station it associates but does not pass data. Do not use
  it to validate the AP data path.
- The RTL8812AU driver is single-vif. While `wlan2` is an AP, adding a second
  vif (monitor or managed) fails with `Operation not supported (-95)`; the AP
  itself stays up. The AP card therefore cannot monitor or inject at the same
  time. Use a second radio for monitor and injection.

## 10. TFTP and NFS for the station

The same card serves the station's boot files and its NFS root. The paths
below are variables so this document stays independent of the local layout:
`TFTP_ROOT` is the TFTP server's root directory, `NFS_ROOT` is the NFS export
root for the station, and `MOUNT_POINT` is where the station mounts it.

- `tftpd-hpa` on UDP 69, jailed to `$TFTP_ROOT`. Kali's unit uses
  `TFTPD_HOME`, not the Debian `/etc/default/tftpd-hpa` file; override:

  ```ini
  /etc/systemd/system/tftpd-hpa.service.d/override.conf
  [Service]
  Environment=TFTPD_HOME=$TFTP_ROOT
  ReadWritePaths=$TFTP_ROOT
  ```

  Contents: `Image` (the 7.3.0-rc2 bench kernel),
  `rk3568-e4ap5g1-itx.dtb`, and `alpine-rootfs.cpio.gz` (the mfs initramfs).

- `nfs-server` (nfs-utils 3.x; the unit is `nfs-server`, not
  `nfs-kernel-server`) exports `$NFS_ROOT` and `$NFS_ROOT-ram` to
  `192.168.0.0/24` with `rw,sync,no_subtree_check,no_root_squash,insecure`.
  Pin `[mountd] port=40001` in `/etc/nfs.conf`; the mfs busybox client needs a
  fixed port.

- The NFS root carries the lab root filesystem (1.8 GB, 43,818 files) plus the
  7.3.0-rc2 module tree under `usr/lib/modules/7.3.0-rc2-ge71d9a28760f`
  (374 MB) and the rtw89 firmware under `usr/lib/firmware/rtw89`.

- The mfs initramfs also carries `rtw89/rtw8851b_fw-1.bin` in
  `/lib/firmware/rtw89` and a `/lib/modules -> $MOUNT_POINT/lib/modules`
  symlink, so the station's driver scripts find the module tree without a
  manual link.

- Services: `systemctl enable --now tftpd-hpa nfs-server nfs-mountd rpcbind`.
  Verification:

  ```sh
  systemctl is-active tftpd-hpa nfs-server nfs-mountd rpcbind
  ss -lunp | grep :69
  exportfs -v
  rpcinfo -p localhost | grep mountd
  ```

- The other SD card still carries the older Debian TFTP/NFS setup. Either card
  can serve the station, but only one is in the slot at a time.

## 11. Recovery

- LUKS recovery passphrase: set when the card was built and recorded with the
  bench owner. A key file slot is also present; both slots open the container.
- Rebuild the LUKS initramfs after a kernel change:

```sh
update-initramfs -u -k $(ls /lib/modules | grep rpt-rpi-v8 | sort -V | tail -1)
```

  The post-update hook copies the result to `/boot/firmware/initramfs.gz`.
- If the AP does not start: `systemctl status lab-ap`, then
  `/var/log/lab-ap/hostapd.log`, then `iw dev wlan2 info`. Check the MAC to name
  mapping first; the most common cause is that `wlan2` is not the RTL8812AU.
- If the card does not reach the AP service, verify the encrypted root mount
  (`findmnt /`) before changing any AP file.

## 12. Change log

- 2026-11-08: first standard bench card. Kali 2026.3 on a LUKS root, RTL8812AU
  as AP, MT7921AU as monitor, `lab-ap` unit, DKMS driver, documented caveats.
