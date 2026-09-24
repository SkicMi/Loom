from __future__ import annotations

import argparse
import sys


def parse_adapter_args(argv: list[str]):
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--first_heading_angle", type=float, default=0.0)
    parser.add_argument("--root_margin", type=float, default=0.04)
    return parser.parse_known_args(argv)


def install_generation_defaults(model_type, first_heading_angle: float, root_margin: float):
    original = model_type.__call__

    def wrapped(self, *args, **kwargs):
        kwargs.setdefault("first_heading_angle", first_heading_angle)
        kwargs.setdefault("root_margin", root_margin)
        return original(self, *args, **kwargs)

    model_type.__call__ = wrapped
    return original


def main(argv: list[str] | None = None) -> int:
    incoming = list(sys.argv[1:] if argv is None else argv)
    options, forwarded = parse_adapter_args(incoming)
    sys.argv = [sys.argv[0], *forwarded]

    from kimodo.model.kimodo_model import Kimodo
    from kimodo.scripts.generate import main as kimodo_main

    install_generation_defaults(Kimodo, options.first_heading_angle, options.root_margin)
    kimodo_main()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
