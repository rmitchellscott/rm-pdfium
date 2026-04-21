# rm-pdfium

[![rm1](https://img.shields.io/badge/rM1-supported-green)](https://remarkable.com/store/remarkable)
[![rm2](https://img.shields.io/badge/rM2-supported-green)](https://remarkable.com/store/remarkable-2)
[![rmpp](https://img.shields.io/badge/rMPP-supported-green)](https://remarkable.com/store/overview/remarkable-paper-pro)
[![rmppm](https://img.shields.io/badge/rMPPM-supported-green)](https://remarkable.com/products/remarkable-paper/pro-move)
<picture>
  <source srcset="assets/rm-pdfium-logo-dark.svg" media="(prefers-color-scheme: dark)">
  <img src="assets/rm-pdfium-logo.svg" alt="rm-pdfium Logo" width="125" align="right">
</picture>
<p align="justify">

A xovi extension that exposes PDF page manipulation via the device's built-in `libpdfium.so`. 

## Dependencies

- [xovi](https://github.com/asivery/rm-xovi-extensions) - Extension framework
    - xovi-message-broker - Required for shell and QML communication

## Installation

1. Ensure dependencies are installed
2. Download the `.so` file for your architecture from the [latest release](https://github.com/rmitchellscott/rm-pdfium/releases/latest) and place it in `/home/root/xovi/extensions.d/` on your reMarkable tablet
    - **reMarkable 1 & 2**: `rm-pdfium-armv7.so`
    - **reMarkable Paper Pro and Paper Pro Move**: `rm-pdfium-aarch64.so`
3. Restart xovi

## Shell Usage

All commands use the xovi-message-broker pipe interface:

```bash
echo '>e<signal>:<params>' > /run/xovi-mb; cat /run/xovi-mb-out
```

### trimPdf

Extract selected pages from a PDF into a new file.

```bash
echo '>etrimPdf:/path/to/source.pdf,/path/to/output.pdf,1,3,5-7' > /run/xovi-mb; cat /run/xovi-mb-out
```

Parameters: `sourcePath,destPath,pageRange`

- **sourcePath** - full path to source PDF
- **destPath** - full path for output PDF
- **pageRange** - 1-indexed, comma-separated page numbers or ranges (e.g. `1,3,5-7`)

Returns `ok` on success or `ERROR: <message>` on failure.

### Available Signals

| Signal | Parameter | Returns |
|--------|-----------|---------|
| `trimPdf` | `sourcePath,destPath,pageRange` | `ok` |

## Building

```bash
./build.sh
```

Builds for both architectures using Docker:
- `rm-pdfium-aarch64.so` - reMarkable Paper Pro
- `rm-pdfium-armv7.so` - reMarkable 2

## License

Copyright (C) 2026 Mitchell Scott

Licensed under the GNU General Public License v3.0.
