#!/usr/bin/env bash
# Intel Open Image Denoise (OIDN 2.5) za LoomTracer, bez gradnje i bez headera.
#
# Loom OIDN ucitava tek pri pokretanju (dlopen, Tracer/Denoise.cpp), pa nije ovisnost gradnje:
# bez njega render koristi vlastiti A-trous filtar. Sluzbene biblioteke dolaze u pip paketu
# pyoidn; odavde se uzmu one za procesor, a s --gpu i za kartice, u tools/oidn/lib.
#
#   tools/oidn/fetch.sh            # jednom, poslije je render s OIDN-om
#   tools/oidn/fetch.sh --gpu      # i uredjaji za kartice (CUDA, HIP, SYCL): OIDN ih sam izabere
#   LOOM_OIDN=/put/do/lib ...      # ili biblioteka negdje drugdje
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
python3 -m pip download --no-deps -q -d "$work" pyoidn==2.5.0.1
cd "$work"
python3 -m zipfile -e pyoidn-*.whl wheel >/dev/null
mkdir -p "$here/lib"
for f in wheel/pyoidn/oidn/lib/libOpenImageDenoise.so* wheel/pyoidn/oidn/lib/libOpenImageDenoise_core.so* \
         wheel/pyoidn/oidn/lib/libOpenImageDenoise_device_cpu.so* wheel/pyoidn/oidn/lib/libtbb.so*; do
    cp -a "$f" "$here/lib/"
done
if [ "${1:-}" = "--gpu" ]; then
    for f in wheel/pyoidn/oidn/lib/libOpenImageDenoise_device_cuda.so* wheel/pyoidn/oidn/lib/libOpenImageDenoise_device_hip.so* \
             wheel/pyoidn/oidn/lib/libOpenImageDenoise_device_sycl.so*; do
        [ -e "$f" ] && cp -a "$f" "$here/lib/"
    done
fi
echo "OIDN u $here/lib:"
ls "$here/lib"
