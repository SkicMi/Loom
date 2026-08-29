#!/usr/bin/env python3
"""Svjetlo koje kruzi oko subjekta, kao niz kadrova i kao snimka.

Prozor to vec radi, ali prozor se ne da poslati nikome ni usporediti s cim. Ovo je isti
kruzni put, samo kadar po kadar na disk - pa se demo da pogledati, a i svaki pojedini kut se
da izmjeriti.

    tools/verify/orbit.py --plate portret.png --depth portret.pfm --near 1.24 --far 3.20 \\
        --fov 40 -o orbita

Izlazi orbita/frame_0000.png ... i orbita.mp4 ako ffmpeg postoji.
"""
import argparse, os, subprocess, sys


def main():
    parser = argparse.ArgumentParser(description="svjetlo koje kruzi, u kadrove i u snimku")
    parser.add_argument("--plate", required=True)
    parser.add_argument("--depth", required=True)
    parser.add_argument("--near", type=float, required=True)
    parser.add_argument("--far", type=float, required=True)
    parser.add_argument("--fov", type=float, default=50.0)
    parser.add_argument("--mask", help="silueta subjekta, za debljinu zaklona")
    parser.add_argument("--frames", type=int, default=72)
    parser.add_argument("--fps", type=int, default=24)
    parser.add_argument("--loom", default="build/LoomApp")
    parser.add_argument("-o", "--output", default="orbita")
    parser.add_argument("--extra", nargs="*", default=[],
                        help="sto god jos ide LoomAppu, recimo --ao 0")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    for index in range(args.frames):
        angle = 360.0 * index / args.frames
        target = os.path.join(args.output, "frame_%04d.png" % index)

        command = [args.loom, args.plate, args.depth, str(args.near), str(args.far),
                   "--fov", str(args.fov), "--angle", "%.4f" % angle, "--save", target]
        if args.mask:
            command += ["--mask", args.mask]
        command += args.extra

        done = subprocess.run(command, capture_output=True, text=True)
        if done.returncode != 0 or not os.path.exists(target):
            sys.exit("LoomApp nije uspio na kutu %.1f:\n%s\n%s"
                     % (angle, done.stdout[-1500:], done.stderr[-1500:]))

        print("  %d/%d  kut %5.1f" % (index + 1, args.frames, angle), end="\r", flush=True)

    print("\nzapisano %d kadrova u %s" % (args.frames, args.output))

    #Snimka je samo udobnost: kadrovi su ono sto se mjeri
    movie = args.output.rstrip("/") + ".mp4"
    try:
        subprocess.run(["ffmpeg", "-y", "-framerate", str(args.fps),
                        "-i", os.path.join(args.output, "frame_%04d.png"),
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", movie],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("snimka: %s  (%d kadrova na %d fps)" % (movie, args.frames, args.fps))
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("ffmpeg nije uspio - kadrovi su svejedno tu")

    return 0


if __name__ == "__main__":
    sys.exit(main())
