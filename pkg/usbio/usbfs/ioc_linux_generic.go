// Copyright 2026 Till0196
// SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later

//go:build linux && !mips && !mipsle && !mips64 && !mips64le

package usbfs

// The ioctl direction values and size width from <asm-generic/ioctl.h>, the
// encoding most Linux architectures inherit: _IOC_NONE is 0, the direction
// sits above a 14-bit size.
const (
	iocNone     = 0
	iocWrite    = 1
	iocRead     = 2
	iocSizeBits = 14
)
