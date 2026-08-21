# PDFium host SDK

The provisioner installs the pinned, non-V8 and non-XFA Linux x86-64 PDFium
SDK outside the mecaps source tree. It verifies the downloaded archive's
SHA-256, required files, recorded GN arguments, CMake linking, and runtime
initialization before publishing the SDK directory.

```sh
./tools/pdfium/provision-linux-x64.sh /workspaces/pdfium-sdk
```

The resulting package is
`/workspaces/pdfium-sdk/pdfium-153.0.8009.0-linux-x64`. It contains the public
headers, shared library, CMake package configuration, upstream and bundled
third-party license notices, `args.gn`, version metadata, provenance, and the
source archive checksum.

Supply that directory as `PDFium_DIR` when configuring a consumer.
