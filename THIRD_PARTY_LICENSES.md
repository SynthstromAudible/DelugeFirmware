# Third-Party Licenses

The Deluge Firmware is distributed under the GNU General Public License
version 3 (see [LICENSE](LICENSE)). Several third-party components are
bundled with the source tree under their own permissive licenses. This
file records each component, where it lives in this repository, its
upstream origin where known, and the full text of its license as found
in the source files.

Adding or removing a vendored dependency should be accompanied by an
update to this file.

---

## ARM NE10

- **Path:** [`src/NE10/`](src/NE10/)
- **Upstream:** https://github.com/projectNe10/Ne10
- **Version:** as captured at integration time (file headers carry `Copyright 2011-16 ARM Limited and Contributors`)
- **License:** BSD 3-Clause

```
Copyright 2011-16 ARM Limited and Contributors.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of ARM Limited nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY ARM LIMITED AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ARM LIMITED AND CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## FatFs (ChaN)

- **Path:** [`src/fatfs/`](src/fatfs/)
- **Upstream:** http://elm-chan.org/fsw/ff/
- **Version:** R0.14b (per `src/fatfs/00readme.txt`)
- **License:** FatFs license (custom one-clause BSD-style permissive license)

```
Copyright (C) 2021, ChaN, all right reserved.

FatFs module is an open source software. Redistribution and use of FatFs in
source and binary forms, with or without modification, are permitted provided
that the following condition is met:

1. Redistributions of source code must retain the above copyright notice,
   this condition and the following disclaimer.

This software is provided by the copyright holder and contributors "AS IS"
and any warranties related to this software are DISCLAIMED.
The copyright owner or contributors be NOT LIABLE for any damages caused
by use of this software.
```

---

## SEGGER Real Time Transfer (RTT)

- **Path:** [`src/RTT/`](src/RTT/)
- **Upstream:** https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/
- **Version:** RTT 6.70f (per file headers)
- **License:** SEGGER RTT license (custom one-clause BSD-style permissive license)

```
(c) 1995 - 2019 SEGGER Microcontroller GmbH

All rights reserved.

SEGGER strongly recommends to not make any changes to or modify the source
code of this software in order to stay compatible with the RTT protocol and
J-Link.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following condition is met:

o Redistributions of source code must retain the above copyright notice,
  this condition and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL SEGGER Microcontroller BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## Marco Paland printf

- **Path:** [`src/lib/printf.c`](src/lib/printf.c), [`src/lib/printf.h`](src/lib/printf.h)
- **Upstream:** https://github.com/mpaland/printf
- **Copyright:** 2014-2019 Marco Paland (info@paland.com), PALANDesign Hannover, Germany
- **License:** MIT

```
The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## Renesas RZ/A1 board support package

- **Path:** most files under [`src/RZA1/`](src/RZA1/) (notable exceptions documented below)
- **Origin:** Renesas Electronics Corporation, distributed with the RZ/A1L SDK
- **Copyright:** 2012-2015 Renesas Electronics Corporation
- **License:** Renesas proprietary; use restricted to Renesas products. The Deluge hardware uses a Renesas RZ/A1L processor, which satisfies the license.

```
DISCLAIMER
This software is supplied by Renesas Electronics Corporation and is only
intended for use with Renesas products. No other uses are authorized. This
software is owned by Renesas Electronics Corporation and is protected under
all applicable laws, including copyright laws.
THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
Renesas reserves the right, without notice, to make changes to this software
and to discontinue the availability of this software. By using this software,
you agree to the additional terms and conditions found by accessing the
following link:
http://www.renesas.com/disclaimer
Copyright (C) 2012 - 2015 Renesas Electronics Corporation. All rights reserved.
```

### Mixed-license files within `src/RZA1/`

- `src/RZA1/diskio.c` carries both a Synthstrom Audible GPLv3-or-later
  copyright header (Synthstrom modifications to glue FatFs to the Deluge
  SD I/O path) and a permissive header from ChaN for the underlying
  MMCv3/SDv1/SDv2 SPI control module:

```
Copyright (C) 2014, ChaN, all right reserved.

* This software is a free software and there is NO WARRANTY.
* No restriction on use. You can use, modify and redistribute it for
  personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
* Redistributions of source code must retain the above copyright notice.
```

- Files under `src/RZA1/` named with a `_Deluge_` infix (for example
  `src/RZA1/spibsc/spibsc_Deluge_setup.c`) contain Synthstrom-authored
  additions licensed under GPLv3-or-later as part of the rest of this
  firmware.

---

## Not third-party: `src/gsl/`

The file [`src/gsl/gsl`](src/gsl/gsl) is a nine-line shim that defines a
`gsl::owner<T>` type alias used as an annotation for clang-tidy. It is
not derived from Microsoft GSL or any other external project despite
the directory name, and is covered by the same GPLv3-or-later license
as the rest of the firmware.
