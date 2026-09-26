#!/usr/bin/env python3
"""Kimodo koji stalno radi: modeli se ucitaju jednom, svako generiranje je samo difuzija.

    kimodo_service.py run <isti argumenti kao kimodo_cli.py>    generiranje (klijent)
    kimodo_service.py range <constraints|splice> ...             kimodo_range.py kroz radnika
    kimodo_service.py serve                                      radnik (klijent ga sam pokrene)
    kimodo_service.py status | stop

ZASTO. Izmjereno 2026-09-26 (RTX 5070, 4 varijante, 200 koraka, 120 kadrova): kimodo_cli.py traje
~44 s, od cega je ~35 s ucitavanje - Llama 8B tekstni enkoder (LLM2Vec, na procesoru) i Kimodo
model - a sama difuzija ~6-10 s. Svako generiranje je to placalo iznova.

KAKO. Radnik drzi modele u memoriji i slusa na UNIX socketu (samo vlasnik, 0600). Klijent posalje
argumente, radnik ih izvede Kimodovim vlastitim `generate.main()` - isti ispis, isti izlazi, isti
BVH - samo s modelom iz predmemorije. Ispis se prosljedjuje redak po redak, pa Loomov posao
(LoomJob.h) vidi isto sto je vidio i prije. Radnik se sam ugasi nakon LOOM_KIMODO_IDLE_S sekundi
bez posla (zadano 1200) da ne drzi ~15 GB RAM-a i karticu.

Kad radnik ne moze krenuti, klijent sam izvede stari put (kimodo_cli / kimodo_range u procesu) -
generiranje nikad ne ovisi o servisu, samo ide brze kad servis radi.
"""
from __future__ import annotations

import contextlib
import fcntl
import io
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXIT_MARK = "\x00LOOM-EXIT "


def socket_path() -> Path:
    runtime = os.environ.get("XDG_RUNTIME_DIR") or f"/tmp/loom-{os.getuid()}"
    Path(runtime).mkdir(mode=0o700, parents=True, exist_ok=True)
    return Path(os.environ.get("LOOM_KIMODO_SOCKET", Path(runtime) / "loom-kimodo.sock"))


def log_path() -> Path:
    base = Path(os.environ.get("LOOM_KIMODO_LOG_DIR", Path.home() / ".cache" / "loom"))
    base.mkdir(parents=True, exist_ok=True)
    return base / "kimodo_service.log"


# -- radnik ----------------------------------------------------------------------------------------

class _LineWriter(io.TextIOBase):
    """stdout/stderr zahtjeva: sve ide klijentu. tqdm pise '\\r', pa se i on prosljedjuje."""

    def __init__(self, conn: socket.socket):
        self.conn = conn

    def write(self, text: str) -> int:
        if text:
            try:
                self.conn.sendall(text.encode("utf-8", "replace"))
            except OSError:
                pass
        return len(text)

    def flush(self) -> None:
        pass

    def isatty(self) -> bool:
        return False


class Worker:
    def __init__(self, fake: bool = False):
        self.fake = fake
        self.models: dict[str, tuple] = {}
        self.text_encoder = None
        self.request_options = {"first_heading_angle": 0.0, "root_margin": 0.04}
        if not fake:
            self._prepare_kimodo()

    def _prepare_kimodo(self) -> None:
        os.environ.setdefault("TEXT_ENCODER_DEVICE", "cpu")
        os.environ["TEXT_ENCODER_MODE"] = "local"
        import kimodo.scripts.generate as generate
        from kimodo.model.kimodo_model import Kimodo
        from kimodo.model.load_model import load_model

        original_load = load_model

        # Isti potpis kao kimodo.load_model; drugi model dijeli vec ucitani tekstni enkoder
        def cached_load(modelname=None, device=None, **kwargs):
            key = f"{modelname}|{device}"
            if key not in self.models:
                if self.text_encoder is not None:
                    kwargs["text_encoder"] = self.text_encoder
                kwargs["return_resolved_name"] = True
                model, resolved = original_load(modelname, device=device, **kwargs)
                self.text_encoder = model.text_encoder
                self.models[key] = (model, resolved)
            model, resolved = self.models[key]
            return (model, resolved) if kwargs.get("return_resolved_name", True) else model

        generate.load_model = cached_load
        self.generate = generate

        # kimodo_cli.py je prvi kut i marginu korijena ubacivao omotom oko Kimodo.__call__; ovdje
        # je omot jedan, a vrijednosti su od trenutnog zahtjeva
        call = Kimodo.__call__
        options = self.request_options

        def wrapped(model_self, *args, **kwargs):
            kwargs.setdefault("first_heading_angle", options["first_heading_angle"])
            kwargs.setdefault("root_margin", options["root_margin"])
            return call(model_self, *args, **kwargs)

        Kimodo.__call__ = wrapped

    def preload(self, model: str = "Kimodo-SOMA-RP-v1.1") -> None:
        if self.fake:
            return
        import torch

        device = "cuda:0" if torch.cuda.is_available() else "cpu"
        self.generate.load_model(model, device=device, default_family="Kimodo", return_resolved_name=True)

    def handle(self, argv: list[str], out: _LineWriter) -> int:
        if self.fake:
            # Za testove: bez modela, samo protokol
            print(f"fake worker pid {os.getpid()} argv {' '.join(argv)}", file=out)
            return int(os.environ.get("LOOM_KIMODO_FAKE_EXIT", "0"))
        if argv and argv[0] == "range":
            sys.path.insert(0, str(HERE))
            import kimodo_range
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                try:
                    return int(kimodo_range.main(argv[1:]) or 0)
                except SystemExit as stop:
                    if isinstance(stop.code, str):
                        print(stop.code, file=out)
                        return 1
                    return int(stop.code or 0)
        sys.path.insert(0, str(HERE))
        import kimodo_cli

        options, forwarded = kimodo_cli.parse_adapter_args(argv)
        self.request_options["first_heading_angle"] = options.first_heading_angle
        self.request_options["root_margin"] = options.root_margin
        saved_argv = sys.argv
        sys.argv = ["kimodo_gen", *forwarded]
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                try:
                    self.generate.main()
                    return 0
                except SystemExit as stop:
                    return int(stop.code or 0) if not isinstance(stop.code, str) else 1
                except Exception as error:  # posao je pao, radnik ostaje
                    import traceback
                    traceback.print_exc(file=out)
                    print(f"kimodo_service: {type(error).__name__}: {error}", file=out)
                    return 1
        finally:
            sys.argv = saved_argv


def serve() -> int:
    path = socket_path()
    fake = os.environ.get("LOOM_KIMODO_FAKE") == "1"
    idle = float(os.environ.get("LOOM_KIMODO_IDLE_S", "1200"))
    if os.environ.get("LOOM_KIMODO_FAKE_CRASH") == "1":
        raise SystemExit("kimodo_service: namjerni pad pri ucitavanju (test)")
    worker = Worker(fake=fake)
    worker.preload()
    with contextlib.suppress(FileNotFoundError):
        path.unlink()
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    old_mask = os.umask(0o177)
    try:
        server.bind(str(path))
    finally:
        os.umask(old_mask)
    os.chmod(path, 0o600)
    server.listen(4)
    server.settimeout(1.0)
    last = time.monotonic()
    print(f"kimodo_service: spreman na {path} (pid {os.getpid()})", flush=True)
    try:
        while time.monotonic() - last < idle:
            try:
                conn, _ = server.accept()
            except socket.timeout:
                continue
            with conn:
                conn.settimeout(None)
                data = b""
                while not data.endswith(b"\n"):
                    chunk = conn.recv(65536)
                    if not chunk:
                        break
                    data += chunk
                try:
                    request = json.loads(data.decode("utf-8"))
                except ValueError:
                    continue
                if request.get("command") == "stop":
                    conn.sendall(f"{EXIT_MARK}0\n".encode())
                    break
                if request.get("command") == "status":
                    conn.sendall(f"kimodo_service pid {os.getpid()}, modela u memoriji {len(worker.models)}\n{EXIT_MARK}0\n".encode())
                    last = time.monotonic()
                    continue
                cwd = request.get("cwd")
                previous = os.getcwd()
                if cwd:
                    os.chdir(cwd)
                started = time.monotonic()
                try:
                    code = worker.handle(request.get("argv", []), _LineWriter(conn))
                finally:
                    os.chdir(previous)
                print(f"kimodo_service: posao {code} za {time.monotonic() - started:.1f} s", flush=True)
                with contextlib.suppress(OSError):
                    conn.sendall(f"\n{EXIT_MARK}{code}\n".encode())
                last = time.monotonic()
    finally:
        server.close()
        with contextlib.suppress(FileNotFoundError):
            path.unlink()
    print("kimodo_service: ugasen (bez posla ili stop)", flush=True)
    return 0


# -- klijent ---------------------------------------------------------------------------------------

def _connect(path: Path) -> socket.socket | None:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(str(path))
        return sock
    except OSError:
        sock.close()
        return None


def _start_worker(path: Path, wait_s: float) -> socket.socket | None:
    """Pokrene radnika ako ne radi. Brava sprijeci da dva klijenta pokrenu dva radnika."""
    lock_file = open(str(path) + ".lock", "w")
    fcntl.flock(lock_file, fcntl.LOCK_EX)
    try:
        sock = _connect(path)
        if sock:
            return sock
        print("Starting the Kimodo service (models load once, then stay in memory)...", flush=True)
        with open(log_path(), "a") as log:
            worker = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "serve"],
                                      stdout=log, stderr=log, stdin=subprocess.DEVNULL,
                                      start_new_session=True, env=os.environ.copy())
        deadline = time.monotonic() + wait_s
        while time.monotonic() < deadline:
            sock = _connect(path)
            if sock:
                return sock
            # Radnik koji je pao pri ucitavanju (npr. bez Hugging Face prijave) ne smije drzati
            # klijenta do isteka roka - prvi pokusaj je tako cekao 600 s prije starog puta
            if worker.poll() is not None:
                print(f"Kimodo service stopped while loading; see {log_path()}", flush=True)
                return None
            time.sleep(0.5)
        return None
    finally:
        fcntl.flock(lock_file, fcntl.LOCK_UN)
        lock_file.close()


def _talk(sock: socket.socket, request: dict) -> int:
    sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
    buffer = ""
    code = 1
    while True:
        chunk = sock.recv(65536)
        if not chunk:
            break
        buffer += chunk.decode("utf-8", "replace")
        if EXIT_MARK in buffer:
            text, _, tail = buffer.partition(EXIT_MARK)
            sys.stdout.write(text)
            code = int(tail.split()[0]) if tail.split() else 1
            break
        # Ispis ide dalje odmah; zadrzi samo moguci pocetak oznake izlaza
        keep = len(EXIT_MARK)
        sys.stdout.write(buffer[:-keep])
        sys.stdout.flush()
        buffer = buffer[-keep:]
    sys.stdout.flush()
    return code


def _fallback(argv: list[str]) -> int:
    print("Kimodo service unavailable; running Kimodo directly (slower).", flush=True)
    if os.environ.get("LOOM_KIMODO_FAKE") == "1":
        return 3    # testovi: stari put se ne izvodi, samo se potvrdi da je izabran
    sys.path.insert(0, str(HERE))
    if argv and argv[0] == "range":
        import kimodo_range
        return int(kimodo_range.main(argv[1:]) or 0)
    import kimodo_cli
    return int(kimodo_cli.main(argv) or 0)


def run(argv: list[str]) -> int:
    path = socket_path()
    if os.environ.get("LOOM_KIMODO_NO_SERVICE") == "1":
        return _fallback(argv)
    sock = _connect(path) or _start_worker(path, float(os.environ.get("LOOM_KIMODO_START_S", "600")))
    if not sock:
        return _fallback(argv)
    with sock:
        return _talk(sock, {"argv": argv, "cwd": os.getcwd()})


def control(command: str) -> int:
    sock = _connect(socket_path())
    if not sock:
        print("kimodo_service: ne radi")
        return 0 if command == "stop" else 1
    with sock:
        return _talk(sock, {"command": command})


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        print(__doc__)
        return 2
    command, rest = argv[0], argv[1:]
    if command == "serve":
        return serve()
    if command in ("status", "stop"):
        return control(command)
    if command == "run":
        return run(rest)
    if command == "range":
        return run(["range", *rest])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
