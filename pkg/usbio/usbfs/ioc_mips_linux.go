// Copyright 2026 Till0196
// SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later

//go:build linux && (mips || mipsle || mips64 || mips64le)

package usbfs

// The ioctl direction values and size width from <asm/ioctl.h> for MIPS,
// whose encoding predates and differs from asm-generic: _IOC_NONE is 1,
// _IOC_WRITE is 4, and the size field is 13 bits, so the direction sits one
// bit lower. Request numbers built with the generic values match nothing in
// a MIPS kernel and every one of them fails with ENOTTY.
const (
	iocNone     = 1
	iocWrite    = 4
	iocRead     = 2
	iocSizeBits = 13
)
