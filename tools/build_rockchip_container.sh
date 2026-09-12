#!/bin/sh
#
# Builds the aarch64 ground-station binary in a container.
#
# Not build_rockchip.sh, which loop-mounts a Debian *bullseye* cloud image and
# chroots into it. That stopped working: bullseye's security pocket has moved to
# the archive, so apt inside the chroot 404s on a dozen packages, and because
# prepare_chroot.sh has no `set -e` it marks itself done anyway - the build then
# fails later with `chroot: failed to run command 'make'`, which points nowhere
# near the actual problem.
#
# This needs no root, no loop device and no disk image: with qemu-aarch64
# registered in binfmt_misc (the F flag - `qemu-user-static` on most
# distributions), an arm64 container runs straight through.
#
# Bookworm on purpose. The binary has to run on the ground station's glibc, and
# building against a newer one than it has is how you get
# `GLIBC_2.39 not found` on a device you cannot easily fix in the field.
set -e

ENGINE="${ENGINE:-$(command -v podman || command -v docker)}"
[ -n "$ENGINE" ] || { echo "need podman or docker" >&2; exit 1; }

IMAGE="${IMAGE:-docker.io/library/debian:bookworm}"
OUT="${OUT:-msposd_rockchip}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

exec "$ENGINE" run --rm --platform linux/arm64 \
	-v "$ROOT":/usr/src/msposd:Z -w /usr/src/msposd \
	-e OUT="$OUT" \
	"$IMAGE" sh -c '
		set -e
		apt-get update -qq
		apt-get install -y -qq --no-install-recommends \
			gcc make pkg-config libevent-dev libcairo2-dev \
			libcurl4-openssl-dev libjpeg-dev libx11-dev libxext-dev
		make -B DRV="$PWD" OUTPUT="$OUT" rockchip
	'
